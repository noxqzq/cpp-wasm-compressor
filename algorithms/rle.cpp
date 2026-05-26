#include <emscripten/emscripten.h> 
#include <string.h>

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    int rleCompress(const char* input, char* output, int inputLen) {
        int outPos = 0;
        int i = 0;
        
        // Process entire input buffer
        while (i < inputLen) {
            char current = input[i];
            int count = 1;
            
            // count identical characters that are together
            // stop if end reached, character changes, or hit max count (255)
            while (i + count < inputLen &&
                   input[i + count] == current &&
                   count < 255) { 
                count++;
            }
            
            // Write RLE pair: [count][character]
            output[outPos++] = count;
            output[outPos++] = current;
            i += count;
        }
        
        return outPos;  // Return size of compressed data
    }
    EMSCRIPTEN_KEEPALIVE
    int rleDecompress(const char* input, char* output, int inputLen) {
        int outPos = 0;
        int i = 0;
        
        // Process compressed data in pairs: [count][character]
        while (i < inputLen) {
            int count = (unsigned char)input[i++];
            char character = input[i++];
            
            // Write character 'count' times
            for (int j = 0; j < count; j++) {
                output[outPos++] = character;
            }
        }
        
        return outPos;
    }
} // extern "C"
