#include <emscripten/emscripten.h>

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    int lz77Compress(const char* input, char* output, int inputLen) {
        // TODO
        return 0;
    }
    
    EMSCRIPTEN_KEEPALIVE
    int lz77Decompress(const char* input, char* output, int inputLen) {
        // TODO
        return 0;
    }
}
