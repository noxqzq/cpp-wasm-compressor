#include <emscripten/emscripten.h>

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    int huffmanCompress(const char* input, char* output, int inputLen) {
        // TODO
        return 0;
    }
    
    EMSCRIPTEN_KEEPALIVE
    int huffmanDecompress(const char* input, char* output, int inputLen) {
        // TODO
        return 0;
    }
}
