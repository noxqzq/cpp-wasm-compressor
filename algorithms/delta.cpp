#include <emscripten/emscripten.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
//  DELTA COMPRESSOR
//
//  Encodes the byte-wise difference between consecutive bytes in the input
//  (self-referential delta: reference = previous output byte, initially 0).
//
//  This is the simplest and most portable form of delta compression — it
//  requires no separate reference file and pairs extremely well with the
//  existing Huffman or RLE passes for a second-stage squeeze, because the
//  delta residuals tend to cluster near zero.
//
//  Wire format (self-delta)
//  ────────────────────────
//  [4 bytes]  original length  (LE uint32)
//  [N bytes]  encoded payload  — one byte per input byte, each being
//             (input[i] - prev) mod 256, where prev starts at 0.
//
//  Wire format (ref-delta)
//  ────────────────────────
//  [4 bytes]  original length  (LE uint32)
//  [4 bytes]  reference length (LE uint32)
//  [min(src,ref) bytes] diff region  — (src[i] - ref[i]) mod 256
//  [remainder bytes]    literal tail (bytes beyond reference length)
//
//  Both variants are exposed so the JS side can pick the right one.
// =============================================================================

extern "C" {

// ── Self-delta ────────────────────────────────────────────────────────────────

EMSCRIPTEN_KEEPALIVE
int deltaCompress(const unsigned char* input, unsigned char* output, int inputLen) {
    if (inputLen == 0) return 0;

    output[0] = (unsigned char)( inputLen & 0xFF);
    output[1] = (unsigned char)((inputLen >>  8) & 0xFF);
    output[2] = (unsigned char)((inputLen >> 16) & 0xFF);
    output[3] = (unsigned char)((inputLen >> 24) & 0xFF);

    unsigned char prev = 0;
    for (int i = 0; i < inputLen; i++) {
        output[4 + i] = (unsigned char)(input[i] - prev);  // mod-256 wraps naturally
        prev = input[i];
    }

    return 4 + inputLen;  // header + payload (same size as input — entropy coder shrinks it)
}

EMSCRIPTEN_KEEPALIVE
int deltaDecompress(const unsigned char* input, unsigned char* output, int inputLen) {
    if (inputLen < 4) {
        return 0;
    }

    int originalLen = (int)input[0]
                    | ((int)input[1] <<  8)
                    | ((int)input[2] << 16)
                    | ((int)input[3] << 24);

    int payloadLen = inputLen - 4;
    if (payloadLen < originalLen) originalLen = payloadLen;  // safety clamp

    unsigned char prev = 0;
    for (int i = 0; i < originalLen; i++) {
        output[i] = (unsigned char)(input[4 + i] + prev);  // inverse: add back
        prev = output[i];
    }

    return originalLen;
}

// ── Reference-delta ───────────────────────────────────────────────────────────
//  Signature matches what index.html passes:
//    deltaCompressRef  (srcPtr, refPtr, outPtr, srcLen, refLen)
//    deltaDecompressRef(srcPtr, refPtr, outPtr, srcLen, refLen)

EMSCRIPTEN_KEEPALIVE
int deltaCompressRef(const unsigned char* src, const unsigned char* ref,
                     unsigned char* output, int srcLen, int refLen) {
    if (srcLen == 0) {
        return 0;
    }

    // Header: original length + reference length
    output[0] = (unsigned char)( srcLen & 0xFF);
    output[1] = (unsigned char)((srcLen >>  8) & 0xFF);
    output[2] = (unsigned char)((srcLen >> 16) & 0xFF);
    output[3] = (unsigned char)((srcLen >> 24) & 0xFF);
    output[4] = (unsigned char)( refLen & 0xFF);
    output[5] = (unsigned char)((refLen >>  8) & 0xFF);
    output[6] = (unsigned char)((refLen >> 16) & 0xFF);
    output[7] = (unsigned char)((refLen >> 24) & 0xFF);

    int p = 8;
    int overlap = srcLen < refLen ? srcLen : refLen;

    // Diff region: (src[i] - ref[i]) mod 256
    for (int i = 0; i < overlap; i++)
        output[p++] = (unsigned char)(src[i] - ref[i]);

    // Literal tail: bytes beyond reference length
    for (int i = overlap; i < srcLen; i++)
        output[p++] = src[i];

    return p;
}

EMSCRIPTEN_KEEPALIVE
int deltaDecompressRef(const unsigned char* input, const unsigned char* ref,
                       unsigned char* output, int inputLen, int refLen) {
    if (inputLen < 8) return 0;

    int originalLen = (int)input[0]
                    | ((int)input[1] <<  8)
                    | ((int)input[2] << 16)
                    | ((int)input[3] << 24);

    // stored refLen not used — caller supplies the actual ref buffer
    (void)refLen;  // suppress unused-parameter warning

    int p = 8;
    int overlap = originalLen < refLen ? originalLen : refLen;

    for (int i = 0; i < overlap; i++)
        output[i] = (unsigned char)(input[p++] + ref[i]);

    for (int i = overlap; i < originalLen; i++)
        output[i] = input[p++];

    return originalLen;
}

} // extern "C"