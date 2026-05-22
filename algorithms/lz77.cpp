#include <emscripten/emscripten.h>
#include "lz77_internal.h"

// All match-finding and decode logic now lives in lz77_internal.h.
// This file only contains the two exported WASM entry points.

extern "C" {

EMSCRIPTEN_KEEPALIVE
int lz77Compress(const unsigned char* input, unsigned char* output, int inputLen) {
    return lz77CompressBuffer(input, output, inputLen);
}

EMSCRIPTEN_KEEPALIVE
int lz77Decompress(const unsigned char* input, unsigned char* output, int inputLen) {
    return lz77DecompressBuffer(input, output, inputLen);
}

} // extern "C"