# WASM File Compressor

> **Browser-native file compression.** Five C++ algorithms compiled to WebAssembly — runs entirely client-side with no server, no uploads, no runtime dependencies.

[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B)](https://en.cppreference.com/w/)
[![WebAssembly](https://img.shields.io/badge/WebAssembly-WASM-654FF0?logo=webassembly)](https://webassembly.org/)
[![Emscripten](https://img.shields.io/badge/Compiled_with-Emscripten-F07800)](https://emscripten.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-22c55e)](LICENSE)

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Algorithms](#algorithms)
   - [RLE — Run-Length Encoding](#1-rle--run-length-encoding)
   - [LZ77 — Sliding Window](#2-lz77--sliding-window)
   - [Huffman — Frequency Coding](#3-huffman--frequency-coding)
   - [ZSTD — LZ77 + Huffman Pipeline](#4-zstd-inspired--lz77--huffman)
   - [Delta — Difference Encoding](#5-delta--difference-encoding)
4. [Project Structure](#project-structure)
5. [Building from Source](#building-from-source)
6. [Running Locally](#running-locally)
7. [Web Interface](#web-interface)
8. [Performance Notes](#performance-notes)
9. [License](#license)

---

## Overview

WASM File Compressor lets you compress and decompress arbitrary binary files directly in the browser. All computation happens inside a WebAssembly module compiled from C++ — the file never leaves your machine.

**Supported algorithms:**

| Algorithm | Best for | Typical ratio |
|-----------|----------|---------------|
| RLE | Bitmap images, sparse binary data | 50–90 % on repetitive input |
| LZ77 | Source code, text, structured data | 40–70 % |
| Huffman | Any file with skewed byte frequencies | 50–80 % |
| ZSTD-inspired | General purpose (LZ77 + Huffman) | 30–60 % |
| Delta | Sequential data chained with Huffman/RLE | varies |

---

## Architecture

```
┌────────────────────────────────────────────┐
│                  Browser                   │
│                                            │
│  ┌──────────┐      ┌──────────────────┐    │
│  │index.html│      │   scripts.js     │    │
│  │styles.css│ ───► │  (UI + FileAPI)  │    |   
│  └──────────┘      └────────┬─────────┘    │
│                              │ ccall/cwrap │
│                   ┌──────────▼─────────┐   │
│                   │  compressor.js     │   │
│                   │ (Emscripten glue)  │   │
│                   └──────────┬─────────┘   │
│                              │             │
│                   ┌──────────▼─────────┐   │
│                   │  compressor.wasm   │   │
│                   │                    │   │
│                   │ rle.cpp            │   │
│                   │ lz77.cpp           │   │
│                   │  + lz77_internal.h │   │
│                   │ huffman.cpp        │   │
│                   │  + huffman_intern. │   │
│                   │ zstd.cpp           │   │
│                   │ delta.cpp          │   │
│                   └────────────────────┘   │
└────────────────────────────────────────────┘
```

**Data flow (compression):**

```
User picks file
      │
      ▼
FileReader.readAsArrayBuffer()  ← JavaScript
      │  Uint8Array
      ▼
Module._malloc(inputLen)        ← Emscripten allocates WASM heap
HEAPU8.set(data, inPtr)         ← Copy JS → WASM memory
      │
      ▼
_rleCompress(inPtr, outPtr, n)  ← C++ runs in WASM sandbox
      │  returns outLen
      ▼
HEAPU8.slice(outPtr, outLen)    ← Copy WASM → JS Uint8Array
Module._free(inPtr/outPtr)
      │
      ▼
Blob + URL.createObjectURL()    ← User downloads result
```

The shared-header pattern (`lz77_internal.h`, `huffman_internal.h`) means the LZ77 and Huffman logic is compiled once but linked into both the standalone files **and** `zstd.cpp` — no duplication.

---

## Algorithms

### 1. RLE — Run-Length Encoding

**Concept:** The simplest possible lossless compressor. Instead of storing each repeated byte individually, store it as a *(count, byte)* pair. `AAABBBCCCC` becomes `3A 3B 4C`.

#### How it works (step by step)

**Compression:**
1. Walk the input byte-by-byte, tracking the current byte value.
2. Count consecutive identical bytes (capped at 255 to fit in one byte).
3. Write a 2-byte token: `[count][byte]`.
4. Advance past the run and repeat.

**Decompression:**
1. Read 2-byte tokens `[count][byte]`.
2. Write `byte` exactly `count` times.

#### Example

```
Input (10 bytes):   A  A  A  B  B  C  C  C  C  C
                    ───────── ───── ─────────────
Output (6 bytes):  [3][A]   [2][B]  [5][C]
```

#### Wire format

```
┌────────┬────────┬────────┬────────┬─ ─ ─┐
│ count₁ │  byte₁ │ count₂ │  byte₂ │     │
│ 1 byte │ 1 byte │ 1 byte │ 1 byte │     │
└────────┴────────┴────────┴────────┴─ ─ ─┘
```

There is no header — the compressed stream is purely token pairs. The output length is always even.

#### Best and worst cases

| Case | Input | Output | Notes |
|------|-------|--------|-------|
| Best | `AAAA...` (255× same byte) | 2 bytes | 127.5× compression |
| Neutral | `ABABAB...` (alternating) | same size | no runs |
| Worst | `\x00\x01\x02\x03...` (all unique) | 2× larger | every byte is a 1-run |

> **Use RLE for:** bitmap images with large solid areas, sparse binary files, palette-indexed images.  
> **Avoid RLE for:** natural language text, pre-compressed data, audio, photos.

---

### 2. LZ77 — Sliding Window

**Concept:** Look back into a *search window* of already-seen bytes. When a sequence of upcoming bytes matches something in the window, write a back-reference *(distance, length)* instead of the raw bytes. Novel bytes that have no prior match are emitted as literals.

This is the foundation of almost every modern general-purpose compressor (gzip, zstd, brotli all use LZ-family passes).

#### Parameters (from `lz77_internal.h`)

| Parameter | Value | Meaning |
|-----------|-------|---------|
| `LZ77_WINDOW_SIZE` | 32 768 bytes | How far back the compressor looks |
| `LZ77_MIN_MATCH` | 3 bytes | Minimum match length to emit a back-reference |
| `LZ77_MAX_MATCH` | 65 535 bytes | Maximum copy length |

#### How it works (step by step)

**Compression (`lz77CompressBuffer`):**
```c++
for each position pos in input:
    windowStart = max(0, pos - 32768)
    search input[windowStart..pos] for longest match with input[pos..]

    if match.length >= 3:
        emit MATCH token: [0x01][len-1][dist_lo][dist_hi]
        advance pos by match.length
    else:
        emit LITERAL token: [0x00][input[pos]]
        advance pos by 1
```

**Decompression (`lz77DecompressBuffer`):**
```C++
for each token:
    0x00 flag → copy next byte verbatim to output
    0x01 flag → read (len, dist), then copy len bytes from
                output[outPos - dist] (may overlap with current write position)
```

The overlapping copy is intentional — it allows a single prior byte to be repeated many times (e.g., distance=1, length=100 copies the previous byte 100 times).

#### Visual: sliding window

```
Already compressed   │   Look-ahead buffer
─────────────────────┼─────────────────────
... b a n a n a      │ b a n a n a ...
    ▲               ▲ ▲
    │←── dist=5 ───►│ │
    └──── match len=6 ┘
Token: [0x01][5][5][0x00]
       flag len-1 dist_lo dist_hi
```

#### Wire format

Each token is one of two forms:

```
LITERAL token (2 bytes):
┌────────┬──────────┐
│  0x00  │   byte   │
│  flag  │  value   │
└────────┴──────────┘

MATCH token (4 bytes):
┌────────┬──────────────┬─────────────┬─────────────┐
│  0x01  │   len - 1    │  dist_lo    │    dist_hi  │
│  flag  │   1 byte     │  1 byte     │    1 byte   │
│        │  (len 1–256) │ ←── distance (LE u16) ──► │
└────────┴──────────────┴─────────────┴─────────────┘
```

The worst-case output size is `2 × inputLen` (every byte is an unmatched literal).

#### Best and worst cases

| Case | Notes |
|------|-------|
| Best | Long repeated substrings within 32 KB of each other |
| Worst | Truly random bytes — every token is a literal, output is ~2× input |
| Interesting | Overlapping matches (`AAAA...`) let one token reproduce many bytes |

> **Use LZ77 for:** source code, JSON/XML, English text, log files, structured binary formats.

---

### 3. Huffman — Frequency Coding

**Concept:** Not all bytes are equally common. In an English text file, `e` and space appear far more often than `q` or `z`. Huffman coding exploits this: assign *shorter* bit patterns to *more frequent* bytes and *longer* ones to rare bytes. The result is a variable-length bitstream that, on average, uses fewer bits per byte than the flat 8-bit encoding.

#### How it works (step by step)

**Building the tree (`huffman_internal.h`):**

```
1. buildFrequencyTable()
   Count occurrences of each of the 256 possible byte values.

2. buildHuffmanTree()
   Insert all non-zero-frequency bytes into a min-heap (priority queue).
   While heap.size > 1:
     left  = heap.pop()    ← lowest frequency
     right = heap.pop()    ← second lowest
     parent.freq = left.freq + right.freq
     heap.push(parent)
   root = heap.pop()

3. generateCodes()
   Walk the tree recursively:
     go left  → append bit 0
     go right → append bit 1
   Leaf nodes receive their final variable-length code.
```

**Example tree for `"aabbc"`:**

```
              (5)
             /    \
           (3)    (2) ← 'a' appears twice, code = 1x
          /   \     \
        (1)   (2)   'a' = 10
        'c'   'b'   'b' = 11
        11    10    'c' = 0
```

Wait — let's trace it properly. Frequencies: `a=2, b=2, c=1`.

```
Heap initially: [c:1] [a:2] [b:2]

Step 1: merge c(1) + a(2) → parent(3)
Heap: [b:2] [parent:3]

Step 2: merge b(2) + parent(3) → root(5)
Heap: [root:5]

Tree:
       root(5)
      /        \
    b(2)     parent(3)
             /       \
           c(1)      a(2)

Codes:  b → 0
        c → 10
        a → 11
```

High-frequency symbol `b` gets 1 bit; low-frequency `c` gets 2 bits.

**Compression (`huffmanCompressBuffer`):**

```
1. Write 4-byte LE original length.
2. Write 512-byte frequency table (256 symbols × 2 bytes each, LE uint16).
3. Encode input as a bitstream using the generated codes.
4. Flush any remaining partial byte (left-padded with zeros).
```

**Decompression (`huffmanDecompressBuffer`):**

```
1. Read original length and rebuild frequency table.
2. Reconstruct the identical tree (deterministic from frequencies).
3. Walk the tree bit-by-bit from the bitstream:
     bit 0 → go left
     bit 1 → go right
     leaf  → emit byte, return to root
   Stop after original_len bytes are emitted.
```

The tree does **not** need to be stored — it is rebuilt identically from the frequency table.

#### Wire format

```
┌──────────────┬────────────────────────┬───────────────────┐
│   4 bytes    │       512 bytes        │      N bytes      │
│ original_len │  freq table (256 × 2)  │  bitstream (MSB)  │
│  (LE u32)    │  each entry: LE u16    │                   │
└──────────────┴────────────────────────┴───────────────────┘
                         ▲
              HUFFMAN_HEADER_SIZE = 516 bytes
```

The header is always 516 bytes regardless of input size, so Huffman is only beneficial for inputs larger than ~516 bytes where the coding gain outweighs the header.

#### Best and worst cases

| Case | Notes |
|------|-------|
| Best | One byte dominates (e.g., 99 % zeros) — that byte gets 1 bit |
| Neutral | Exactly 256 distinct bytes, all equally frequent — codes are all 8 bits, no gain |
| Worst | Already random/encrypted data — adds 516-byte header with no bit savings |

> **Use Huffman for:** files where certain bytes dominate. Often used as a second pass after LZ77 (which is exactly what ZSTD does).

---

### 4. ZSTD-Inspired — LZ77 + Huffman

**Concept:** Combine both LZ77 and Huffman into a two-stage pipeline. LZ77 exploits *repetition* (replaces duplicate byte sequences with back-references). The LZ77 token stream itself has a non-uniform byte distribution — Huffman then exploits that *statistical redundancy* to squeeze the token stream further.

This is the same conceptual pipeline used by the real Zstandard format (and gzip before it).

#### Pipeline

```
             ┌─────────────────────────────┐
             │         zstdCompress()      │
             └──────────────┬──────────────┘
                            │
             ┌──────────────▼──────────────┐
  Input ───► │  Stage 1: lz77CompressBuffer│ ──► Token stream
             └─────────────────────────────┘          │
                                                      │
             ┌─────────────────────────────┐          │
  Output ◄── │ Stage 2: huffmanCompressBuffer◄────────┘
             └─────────────────────────────┘

             ┌─────────────────────────────┐
             │        zstdDecompress()     │
             └──────────────┬──────────────┘
                            │
             ┌──────────────▼──────────────┐
  Input ───► │  Stage 1: huffmanDecompress │ ──► Token stream
             └─────────────────────────────┘          │
                                                      │
             ┌─────────────────────────────┐          │
  Output ◄── │  Stage 2: lz77Decompress   │◄──────────┘
             └─────────────────────────────┘
```

Both stages reuse the internal helpers from `lz77_internal.h` and `huffman_internal.h` — no logic duplication.

#### Wire format

```
┌───────────┬──────────────┬────────────┬──────────────────────────────┐
│  4 bytes  │   4 bytes    │  4 bytes   │            N bytes           │
│   magic   │ original_len │   lz_len   │  Huffman(LZ77(input))        │
│ "ZSTD"    │  (LE u32)    │  (LE u32)  │  (Huffman bitstream)         │
│5A 53 54 44│              │            │                              │
└───────────┴──────────────┴────────────┴──────────────────────────────┘
```

`lz_len` stores the size of the intermediate LZ77 token stream so decompression can allocate an exact buffer.

> **Use ZSTD-inspired for:** general-purpose compression where you want the best ratio. It outperforms RLE, LZ77-only, and Huffman-only on most real files.

---

### 5. Delta — Difference Encoding

**Concept:** Instead of storing absolute byte values, store the *difference* between consecutive bytes. For slowly changing data (like audio samples, sensor readings, or similar version of a file), the deltas cluster near zero, making the resulting byte stream much more compressible by a follow-up entropy coder like Huffman or RLE.

Delta alone does **not** reduce file size (the output is the same number of bytes). It *reshapes* the data to make it more amenable to a second compression pass.

Two modes are available:

#### 5a. Self-Delta

References only the previous byte within the same file. No separate reference file needed.

**Algorithm:**
```
Compress:
  prev = 0
  for each byte b in input:
      output = (b - prev) mod 256
      prev = b

Decompress:
  prev = 0
  for each delta d in input:
      output = (d + prev) mod 256
      prev = output
```

**Example:**
```
Input:         [ 72, 101, 108, 108, 111 ]   ("Hello")
Previous:      [  0,  72, 101, 108, 108 ]
Delta output:  [ 72,  29,   7,   0,   3 ]   ← clusters near 0
```

The `0` in position 3 (letter `l` repeated) becomes an actual zero delta — trivially compressible.

#### Wire format (self-delta)

```
┌──────────────┬─────────────────────────────────────┐
│   4 bytes    │              N bytes                 │
│ original_len │  (input[i] - prev) mod 256 per byte  │
│  (LE u32)    │                                      │
└──────────────┴─────────────────────────────────────┘
```

#### 5b. Reference-Delta

Diffs the *target* file against a separate *reference* file byte-by-byte. Used when you have two versions of a file (e.g., software update v1 → v2) and want to transmit only the differences.

**Algorithm:**
```
overlap = min(srcLen, refLen)

Compress:
  for i in 0..overlap:     output[i] = (src[i] - ref[i]) mod 256
  for i in overlap..srcLen: output[i] = src[i]   ← literal tail

Decompress:
  for i in 0..overlap:     output[i] = (delta[i] + ref[i]) mod 256
  for i in overlap..srcLen: output[i] = delta[i]  ← literal tail
```

#### Wire format (ref-delta)

```
┌──────────┬──────────┬───────────────────────────┬────────────────┐
│ 4 bytes  │ 4 bytes  │       overlap bytes       │   tail bytes   │
│ src_len  │ ref_len  │  (src[i] - ref[i]) mod 256│ src[overlap..] │
│ (LE u32) │ (LE u32) │                           │ (literal)      │
└──────────┴──────────┴───────────────────────────┴────────────────┘
```

#### Chaining

The UI supports chaining Delta with a follow-up Huffman or RLE pass:

```
Self-delta only:       Input ─► Δ ─► Output (~same size, reshaped)
Self-delta + Huffman:  Input ─► Δ ─► Huffman ─► Output (smaller)
Ref-delta + RLE:       Input ─► Δ(ref) ─► RLE ─► Output (smaller)
```

When decoding a chained file, reverse the pipeline: undo the outer coder first, then undo the delta.

> **Use Delta for:** consecutive sensor readings, audio PCM samples, version-to-version file patches (ref-delta), or any data that changes slowly between consecutive bytes.

---

## Project Structure

```
wasm-compressor/
├── algorithms/
│   ├── rle.cpp              # Run-Length Encoding: compress + decompress
│   ├── lz77_internal.h      # LZ77 core: findMatch, compressBuffer, decompressBuffer
│   ├── lz77.cpp             # LZ77 WASM exports (thin wrapper around _internal.h)
│   ├── huffman_internal.h   # Huffman core: tree, heap, code gen, encode/decode
│   ├── huffman.cpp          # Huffman WASM exports (thin wrapper)
│   ├── zstd.cpp             # ZSTD pipeline: LZ77 → Huffman (reuses both _internal.h)
│   └── delta.cpp            # Delta: self-delta and reference-delta variants
├── index.html               # Single-page UI
├── scripts.js               # FileAPI + WASM bridge + result display
├── styles.css               # Styling
├── compressor.js            # ← generated by emcc (Emscripten JS glue)
├── compressor.wasm          # ← generated by emcc (compiled algorithms)
├── .gitignore
├── LICENSE
└── README.md
```

> `compressor.js` and `compressor.wasm` are build artifacts — they are **not** in source control and must be generated by following the build steps below.

---

## Building from Source

### Prerequisites

- **Linux, macOS, or WSL2** (Windows native is unsupported by Emscripten)
- **Python 3** (for the local dev server)
- **Git**
- ~2 GB disk space for the Emscripten SDK

### Step 1 — Install Emscripten

```bash
# Clone the Emscripten SDK
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install and activate the latest stable release
./emsdk install latest
./emsdk activate latest

# Add emcc to your PATH (re-run this in every new shell, or add to .bashrc)
source ./emsdk_env.sh

# Verify
emcc --version
# Expected: emcc (Emscripten ...) 3.x.x
```

### Step 2 — Compile to WebAssembly

Run this from the repository root (the folder containing `algorithms/`):

```bash
emcc \
  algorithms/delta.cpp \
  algorithms/huffman.cpp \
  algorithms/lz77.cpp \
  algorithms/rle.cpp \
  algorithms/zstd.cpp \
  -I algorithms \
  -O2 \
  -s WASM=1 \
  -s EXPORTED_FUNCTIONS='[
    "_malloc","_free",
    "_rleCompress","_rleDecompress",
    "_lz77Compress","_lz77Decompress",
    "_huffmanCompress","_huffmanDecompress",
    "_zstdCompress","_zstdDecompress",
    "_deltaCompress","_deltaDecompress",
    "_deltaCompressRef","_deltaDecompressRef"
  ]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAPU32"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s NO_EXIT_RUNTIME=1 \
  -o compressor.js
```

This produces two files: `compressor.js` (Emscripten glue) and `compressor.wasm` (compiled algorithms).

#### Compiler flags explained

| Flag | Purpose |
|------|---------|
| `-I algorithms` | Makes `#include "lz77_internal.h"` resolve correctly |
| `-O2` | Optimization level 2 — good balance of build speed and output size |
| `WASM=1` | Output WebAssembly (not asm.js) |
| `EXPORTED_FUNCTIONS` | Functions visible to JavaScript (must include `_malloc`/`_free` for manual memory management) |
| `EXPORTED_RUNTIME_METHODS` | Emscripten helpers exposed to JS (`HEAPU8` for typed array access) |
| `ALLOW_MEMORY_GROWTH=1` | WASM heap can grow beyond its initial size |
| `NO_EXIT_RUNTIME=1` | Don't tear down the runtime when `main()` returns (there is no `main()`) |

---

## Running Locally

WebAssembly requires a proper HTTP server — opening `index.html` via `file://` will fail due to CORS restrictions on `.wasm` loading.

```bash
# Python 3 (simplest)
python3 -m http.server 8000

# Node.js (if you have it)
npx serve .

# Then open:
# http://localhost:8000
```

---

## Web Interface

### Algorithm Selector

Click any algorithm button at the top to switch modes. The panel below updates immediately.

```
[ RLE ] [ LZ77 ] [ Huffman ] [ ZSTD ] [ Delta ]
```

### Standard Panel (RLE / LZ77 / Huffman / ZSTD)

1. Drop a file onto the drop zone or click to browse.
2. Click **Compress** to compress it, **Decompress** to reverse a previously compressed file.
3. The result card shows:
   - **Original** — input file size
   - **Output** — compressed/decompressed size
   - **Ratio** — `output / original × 100%` (below 100% = smaller)
   - **Time** — wall-clock time measured with `performance.now()`

### Delta Panel

Delta has two sub-modes:

**Self-delta** — one file only; diffs the file against itself (previous byte).

**Reference-delta** — two files: a *target* (the file to compress) and a *reference* (a baseline file you will keep). The target is diff'd against the reference byte-by-byte.

**Chain with Huffman/RLE** — apply a second compression pass on top of the delta output. This is usually necessary to achieve real size reduction, since delta alone just reshapes the data.

### Memory Management

The JS layer calls `Module._malloc()` to allocate WASM heap buffers, copies data in with `HEAPU8.set()`, runs the C++ function, then copies the result out and calls `Module._free()`. Output buffers are sized at `8× input + 64 bytes` to handle worst-case decompression expansion.

---

## Performance Notes

- **All computation is synchronous** on the main thread. Large files (> 50 MB) may cause the browser to appear unresponsive for a moment.
- The `performance.now()` timer shown in the UI measures only the WASM execution time, not file I/O.
- **LZ77 is O(n × W)** in the worst case (n = input size, W = window size). With a 32 KB window this is fast for files up to ~50 MB; larger files will be slow.
- **ZSTD** runs two passes (LZ77 then Huffman) so it takes roughly twice the time of either alone, but generally achieves the best ratio.
- **RLE and Delta** are O(n) and extremely fast.
- **Huffman** is O(n) for encoding/decoding once the tree is built; tree construction is O(k log k) where k ≤ 256 symbols.

---

## License

MIT — see [LICENSE](LICENSE).