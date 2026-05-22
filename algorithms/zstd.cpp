#include <emscripten/emscripten.h>
#include <stdlib.h>
#include "lz77_internal.h"
#include "huffman_internal.h"

// =============================================================================
//  zstd.cpp  —  zstd-inspired compressor
//
//  Pipeline:
//    compress:   input  →  LZ77 token stream  →  Huffman bitstream
//    decompress: input  →  Huffman decode     →  LZ77 decode  →  output
//
//  Wire format:
//    [4 bytes]  magic        0x5A 0x53 0x54 0x44  ("ZSTD")
//    [4 bytes]  original_len LE uint32
//    [4 bytes]  lz_len       LE uint32  (byte size of the raw LZ token stream)
//    [N bytes]  Huffman-compressed LZ token stream
//
//  The LZ and Huffman layers are shared with lz77.cpp and huffman.cpp via
//  their respective *_internal.h headers — no logic is duplicated here.
// =============================================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE
int zstdCompress(const unsigned char* input, unsigned char* output, int inputLen) {
    if (inputLen == 0) return 0;

    // ── Stage 1: LZ77 ────────────────────────────────────────────────────────
    // Worst-case: every byte becomes a 2-byte literal token.
    int lzBufSize = inputLen * 2 + 16;
    unsigned char* lzBuf = (unsigned char*)malloc(lzBufSize);
    if (!lzBuf) return 0;

    int lzLen = lz77CompressBuffer(input, lzBuf, inputLen);

    // ── Stage 2: Huffman ─────────────────────────────────────────────────────
    int hufBufSize = HUFFMAN_HEADER_SIZE + lzLen + 8;
    unsigned char* hufBuf = (unsigned char*)malloc(hufBufSize);
    
    if (!hufBuf) {
        free(lzBuf);
        return 0;
    }

    int hufLen = huffmanCompressBuffer(lzBuf, lzLen, hufBuf);
    free(lzBuf);

    // ── Outer envelope ───────────────────────────────────────────────────────
    int p = 0;
    output[p++] = 0x5A; output[p++] = 0x53;  // 'Z' 'S'
    output[p++] = 0x54; output[p++] = 0x44;  // 'T' 'D'

    output[p++] = (unsigned char)( inputLen & 0xFF);
    output[p++] = (unsigned char)((inputLen >>  8) & 0xFF);
    output[p++] = (unsigned char)((inputLen >> 16) & 0xFF);
    output[p++] = (unsigned char)((inputLen >> 24) & 0xFF);

    output[p++] = (unsigned char)(lzLen & 0xFF);
    output[p++] = (unsigned char)((lzLen >>  8) & 0xFF);
    output[p++] = (unsigned char)((lzLen >> 16) & 0xFF);
    output[p++] = (unsigned char)((lzLen >> 24) & 0xFF);

    for (int i = 0; i < hufLen; i++) output[p++] = hufBuf[i];
    free(hufBuf);
    return p;
}

EMSCRIPTEN_KEEPALIVE
int zstdDecompress(const unsigned char* input, unsigned char* output, int inputLen) {
    if (inputLen < 12) {
        return 0;
    }

    int p = 0;

    if (input[p] != 0x5A || input[p+1] != 0x53 ||
        input[p+2] != 0x54 || input[p+3] != 0x44) {
        return 0;
    }

    p += 4;

    int originalLen = (int)input[p]
                    | ((int)input[p+1] <<  8)
                    | ((int)input[p+2] << 16)
                    | ((int)input[p+3] << 24);
    p += 4;

    int lzLen = (int)input[p]
              | ((int)input[p+1] <<  8)
              | ((int)input[p+2] << 16)
              | ((int)input[p+3] << 24);
    p += 4;

    if (lzLen <= 0 || originalLen <= 0) {
        return 0;
    }

    // ── Stage 1: Huffman decode → LZ token stream ────────────────────────────
    unsigned char* lzBuf = (unsigned char*)malloc(lzLen + 8);
    if (!lzBuf) return 0;

    int decodedLzLen = huffmanDecompressBuffer(input + p, inputLen - p, lzBuf);
    if (decodedLzLen <= 0) { 
        free(lzBuf); 
        return 0;
    }

    // ── Stage 2: LZ77 decode → original data ─────────────────────────────────
    int result = lz77DecompressBuffer(lzBuf, output, decodedLzLen);
    free(lzBuf);

    return result < originalLen ? result : originalLen;
}

} // extern "C"