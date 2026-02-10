#include <emscripten/emscripten.h> 
#include <string.h>


// extern "C" prevents C++ name mangling so JavaScript can find the function
extern "C" {
    // EMSCRIPTEN_KEEPALIVE ensures the function isn't removed during optimization
    EMSCRIPTEN_KEEPALIVE
    int rleCompress(const char* input, char* output, int inputLen) {
        int outPos = 0;  // write pos in output buffer
        int i = 0;       // read pos in input buffer
        
        // Process entire input buffer
        while (i < inputLen) {
            char current = input[i];  // char thats being counted
            int count = 1;            // count starts at 1
            
            // count identical characters that are together
            // stop if end reached, character changes, or hit max count (255)
            while (i + count < inputLen &&           // not past end
                   input[i + count] == current &&    // same char
                   count < 255) {                    // less than max
                count++;
            }
            
            // Write RLE pair: [count][character]
            output[outPos++] = count;      // How many times
            output[outPos++] = current;    // Which character
            i += count;  // Skip all the characters we just counted
        }
        
        return outPos;  // Return size of compressed data
    }
    EMSCRIPTEN_KEEPALIVE
    int rleDecompress(const char* input, char* output, int inputLen) {
        int outPos = 0;  // Current write position in output buffer
        int i = 0;       // Current read position in input buffer
        
        // Process compressed data in pairs: [count][character]
        while (i < inputLen) {
            int count = (unsigned char)input[i++];  // Read count
            char character = input[i++];             // Read character
            
            // Write character 'count' times
            for (int j = 0; j < count; j++) {
                output[outPos++] = character;
            }
        }
        
        return outPos;  // Return size of decompressed data
    }
}
