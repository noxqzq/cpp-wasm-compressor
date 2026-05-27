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
emcc algorithms/delta.cpp algorithms/huffman.cpp algorithms/lz77.cpp algorithms/rle.cpp algorithms/zstd.cpp \
  -I algorithms \
  -O2 \
  -s WASM=1 \
  -s EXPORTED_FUNCTIONS='["_malloc","_free","_rleCompress","_rleDecompress","_lz77Compress","_lz77Decompress","_huffmanCompress","_huffmanDecompress","_zstdCompress","_zstdDecompress","_deltaCompress","_deltaDecompress","_deltaCompressRef","_deltaDecompressRef"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAPU32"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s NO_EXIT_RUNTIME=1 \
  -o compressor.js
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