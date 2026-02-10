#include <emscripten/emscripten.h>
#include <string.h>

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    int compress(const char* input, char* output, int inputLen) {
        int outPos = 0;
        int i = 0;
        
        while (i < inputLen) {
            char current = input[i];
            int count = 1;
            
            while (i + count < inputLen && input[i + count] == current && count < 255) {
                count++;
            }
            
            output[outPos++] = count;
            output[outPos++] = current;
            i += count;
        }
        
        return outPos;
    }
}
