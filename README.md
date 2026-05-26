# WASM File Compressor

Browser-based file compression tool using C++ and WebAssembly. Performs high-performance compression directly in the browser with no server-side processing.

## Project Structure
```
wasm-compressor/
├── algorithms/
│   ├── delta.cpp                # Delta Compression (Self-delta RLE or Huffman Chaining
│   │                                                 and Reference Delta)
│   ├── huffman_internal.h       # Huffman Internals (shared by huffman.cpp and zstd.cpp)
│   ├── huffman.cpp              # Huffman Encoding
│   ├── lz77_internal.h          # LZ77 Internals (shared by lz77.cpp and zstd.cpp)
│   ├── lz77.cpp                 # LZ77 Dictionary Compression
│   ├── rle.cpp                  # Run-Length Encoding
│   └── zstd.cpp                 # Zstandard-inspired Compression (LZ77 + Huffman)
├── index.html                   # Web interface
├── scripts.js                   # UI logic
├── styles.css                   # Styling
├── .gitignore
└── README.md
```

## Use Instructions

#### Install Emscripten.

- google it

---

#### Compile to WebAssembly:

```
emcc \
  algorithms/rle.cpp \
  algorithms/lz77.cpp \
  algorithms/huffman.cpp \
  algorithms/delta.cpp \
  algorithms/zstd.cpp \
  algorithms/dct.cpp \
  -I algorithms \
  -o compression.js \
  -O2 \
  -s WASM=1 \
  -s MODULARIZE=0 \
  -s EXPORTED_FUNCTIONS='["_malloc","_free","_rleCompress","_rleDecompress","_lz77Compress","_lz77Decompress","_huffmanCompress","_huffmanDecompress","_zstdCompress","_zstdDecompress","_deltaCompress","_deltaDecompress","_deltaCompressRef","_deltaDecompressRef","_dctCompress","_dctDecompress"]' \
  -s EXPORTED_RUNTIME_METHODS='["HEAPU8","HEAP32"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -lm
```
---
#### Start local server:

```bash
python3 -m http.server 8000
Open localhost:8000 in browser.
```
---
### Algorithms

<b>RLE:</b> Run-Length Encoding - compresses repeated charactersls

<b>LZ77:</b>Sliding Window-based - finds repeated patterns

<b>Huffman:</b> Frequency-based - encodes common bytes with fewer bits