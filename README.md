# WASM File Compressor

Browser-based file compression tool using C++ and WebAssembly. Performs high-performance compression directly in the browser with no server-side processing.

## Project Structure
```
wasm-compressor/
├── algorithms/
│ ├── rle.cpp                  # Run-Length Encoding
│ ├── lz77.cpp                 # LZ77 Dictionary Compression
│ └── huffman.cpp              # Huffman Encoding
├── index.html # Web interface
├── compressor.cpp              # Main compression entry point
├── .gitignore
└── README.md
```

## Use Instructions

#### Install Emscripten.

- google it

---

#### Compile to WebAssembly:

```
emcc compressor.cpp -o compressor.js -s WASM=1 \
  -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAP8"]' \
  -s EXPORTED_FUNCTIONS='["_rleCompress","_rleDecompress","_lz77Compress","_lz77Decompress","_huffmanCompress","_huffmanDecompress","_malloc","_free"]'
Running
```
---
#### Start local server:

```bash
python3 -m http.server 8000
Open localhost:8000 in browser.
```
---
### Algorithms

<b>RLE:</b> Run-Length Encoding - compresses repeated characters

<b>LZ77:</b> Dictionary-based - finds repeated patterns

<b>Huffman:</b> Frequency-based - encodes common bytes with fewer bits