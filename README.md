# WASM File Compressor

Browser-based file compression tool using C++ and WebAssembly. Performs high-performance compression directly in the browser with no server-side processing.

## Project Structure
```
wasm-compressor/
├── algorithms/
│   ├── huffman_internal.h       # Huffman Internals (Functions for huffman.cpp and Zstd.cpp)
│   ├── rle.cpp                  # Run-Length Encoding
│   ├── lz77.cpp                 # LZ77 Dictionary Compression
│   ├── huffman.cpp              # Huffman Encoding
│   └── zstd.cpp                 # Zstandard Compression
├── index.html                   # Web interface
├── .gitignore
└── README.md
```

## Use Instructions

#### Install Emscripten.

- google it

---

#### Compile to WebAssembly:

```
emcc algorithms/rle.cpp algorithms/lz77.cpp algorithms/huffman.cpp algorithms/delta.cpp algorithms/zstd.cpp \
  -O2 \
  -I algorithms \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="CompressionModule" \
  -s EXPORTED_FUNCTIONS='["_rleCompress","_rleDecompress","_lz77Compress","_lz77Decompress","_huffmanCompress","_huffmanDecompress","_deltaCompress","_deltaDecompress","_deltaCompressRef","_deltaDecompressRef","_zstdCompress","_zstdDecompress","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -o compression.js
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