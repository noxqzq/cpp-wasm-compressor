#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "huffman_internal.h"   // huffmanCompressBuffer, huffmanDecompressBuffer

// =============================================================================
//  dct.cpp  —  JPEG-inspired DCT image compressor
//
//  Pipeline (compress):
//    Raw RGBA → YCbCr → 8×8 blocks → Forward DCT → Quantize → Zigzag →
//    raw coefficient bytes → huffmanCompressBuffer → channel blob
//
//  Pipeline (decompress):
//    channel blob → huffmanDecompressBuffer → raw coefficient bytes →
//    Dezigzag → Dequantize → Inverse DCT → YCbCr → RGBA
//
//  Huffman layer is provided by huffman_internal.h (shared with huffman.cpp
//  and zstd.cpp) — no Huffman code lives in this file.
//
//  Wire format:
//    [4 bytes]  magic        "DCTZ"
//    [4 bytes]  width        LE uint32
//    [4 bytes]  height       LE uint32
//    [1 byte]   quality      1–100
//    [4 bytes]  yLen         LE uint32  (byte length of Y  channel blob)
//    [4 bytes]  cbLen        LE uint32  (byte length of Cb channel blob)
//    [4 bytes]  crLen        LE uint32  (byte length of Cr channel blob)
//    [yLen]     Y  channel   Huffman-compressed coefficient stream
//    [cbLen]    Cb channel   Huffman-compressed coefficient stream
//    [crLen]    Cr channel   Huffman-compressed coefficient stream
//
//  Quality scaling (JPEG-style):
//    quality < 50:  scale = 5000 / quality
//    quality >= 50: scale = 200 - 2 * quality
//    Each table entry = clamp((entry * scale + 50) / 100, 1, 255)
// =============================================================================

// ── Constants ─────────────────────────────────────────────────────────────────

#define BLOCK   8
#define BLOCK2  64    // 8×8

// Standard JPEG luma quantization table (quality 50 baseline)
static const int LUMA_QUANT[BLOCK2] = {
    16, 11, 10, 16,  24,  40,  51,  61,
    12, 12, 14, 19,  26,  58,  60,  55,
    14, 13, 16, 24,  40,  57,  69,  56,
    14, 17, 22, 29,  51,  87,  80,  62,
    18, 22, 37, 56,  68, 109, 103,  77,
    24, 35, 55, 64,  81, 104, 113,  92,
    49, 64, 92, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103,  99
};

// Standard JPEG chroma quantization table (quality 50 baseline)
static const int CHROMA_QUANT[BLOCK2] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

// Zigzag scan order for 8×8 block (JPEG standard)
static const int ZIGZAG[BLOCK2] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

// ── Math helpers ──────────────────────────────────────────────────────────────

static inline int clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ── Quality table scaling ─────────────────────────────────────────────────────

static void buildQuantTable(const int* base, int quality, int* out) {
    int scale = quality < 50 ? (5000 / quality) : (200 - 2 * quality);
    for (int i = 0; i < BLOCK2; i++)
        out[i] = clamp((base[i] * scale + 50) / 100, 1, 255);
}

// ── Color space conversion ────────────────────────────────────────────────────

static void rgbToYCbCr(unsigned char r, unsigned char g, unsigned char b,
                        float* Y, float* Cb, float* Cr) {
    *Y  =  0.299f   * r + 0.587f   * g + 0.114f   * b;
    *Cb = -0.16874f * r - 0.33126f * g + 0.5f     * b + 128.0f;
    *Cr =  0.5f     * r - 0.41869f * g - 0.08131f * b + 128.0f;
}

static void yCbCrToRgb(float Y, float Cb, float Cr,
                        unsigned char* r, unsigned char* g, unsigned char* b) {
    Cb -= 128.0f; Cr -= 128.0f;
    *r = (unsigned char)clamp((int)(Y                          + 1.402f   * Cr), 0, 255);
    *g = (unsigned char)clamp((int)(Y - 0.34414f * Cb - 0.71414f * Cr        ), 0, 255);
    *b = (unsigned char)clamp((int)(Y + 1.772f   * Cb                         ), 0, 255);
}

// ── Forward DCT (AAN butterfly, separable row/column passes) ──────────────────

static void forwardDCT(float* block) {
    static const float C1 = 0.9807852804f, C2 = 0.9238795325f,
                       C3 = 0.8314696123f, C4 = 0.7071067812f,
                       C5 = 0.5555702330f, C6 = 0.3826834324f,
                       C7 = 0.1950903220f;

    for (int row = 0; row < BLOCK; row++) {
        float* b = block + row * BLOCK;
        float t0=b[0]+b[7], t7=b[0]-b[7], t1=b[1]+b[6], t6=b[1]-b[6],
              t2=b[2]+b[5], t5=b[2]-b[5], t3=b[3]+b[4], t4=b[3]-b[4];
        float s0=t0+t3, s3=t0-t3, s1=t1+t2, s2=t1-t2;
        b[0]=(s0+s1)*C4; b[4]=(s0-s1)*C4;
        b[2]=s2*C6+s3*C2; b[6]=s3*C6-s2*C2;
        float u0=t4*C7+t7*C1, u1=t5*C3+t6*C5,
              u2=t5*C5-t6*C3, u3=t7*C7-t4*C1;
        b[1]=u0+u1; b[7]=u0-u1; b[5]=u3+u2; b[3]=u3-u2;
    }

    for (int col = 0; col < BLOCK; col++) {
        float t0=block[0*BLOCK+col]+block[7*BLOCK+col],
              t7=block[0*BLOCK+col]-block[7*BLOCK+col],
              t1=block[1*BLOCK+col]+block[6*BLOCK+col],
              t6=block[1*BLOCK+col]-block[6*BLOCK+col],
              t2=block[2*BLOCK+col]+block[5*BLOCK+col],
              t5=block[2*BLOCK+col]-block[5*BLOCK+col],
              t3=block[3*BLOCK+col]+block[4*BLOCK+col],
              t4=block[3*BLOCK+col]-block[4*BLOCK+col];
        float s0=t0+t3, s3=t0-t3, s1=t1+t2, s2=t1-t2;
        block[0*BLOCK+col]=(s0+s1)*C4; block[4*BLOCK+col]=(s0-s1)*C4;
        block[2*BLOCK+col]=s2*C6+s3*C2; block[6*BLOCK+col]=s3*C6-s2*C2;
        float u0=t4*C7+t7*C1, u1=t5*C3+t6*C5,
              u2=t5*C5-t6*C3, u3=t7*C7-t4*C1;
        block[1*BLOCK+col]=u0+u1; block[7*BLOCK+col]=u0-u1;
        block[5*BLOCK+col]=u3+u2; block[3*BLOCK+col]=u3-u2;
    }
}

// ── Inverse DCT ───────────────────────────────────────────────────────────────

static void inverseDCT(float* block) {
    static const float C1 = 0.9807852804f, C2 = 0.9238795325f,
                       C3 = 0.8314696123f, C4 = 0.7071067812f,
                       C5 = 0.5555702330f, C6 = 0.3826834324f,
                       C7 = 0.1950903220f;

    for (int col = 0; col < BLOCK; col++) {
        float b0=block[0*BLOCK+col]*C4, b4=block[4*BLOCK+col]*C4,
              b1=block[1*BLOCK+col],    b2=block[2*BLOCK+col],
              b3=block[3*BLOCK+col],    b5=block[5*BLOCK+col],
              b6=block[6*BLOCK+col],    b7=block[7*BLOCK+col];
        float s0=b0+b4, s1=b0-b4,
              s2=b2*C6-b6*C2, s3=b2*C2+b6*C6;
        float t0=s0+s3, t1=s1+s2, t2=s1-s2, t3=s0-s3;
        float p0=b1*C1+b3*C3+b5*C5+b7*C7,
              p1=b1*C3-b3*C7-b5*C1-b7*C5,
              p2=b1*C5-b3*C1+b5*C7+b7*C3,
              p3=b1*C7-b3*C5+b5*C3-b7*C1;
        block[0*BLOCK+col]=t0+p0; block[7*BLOCK+col]=t0-p0;
        block[1*BLOCK+col]=t1+p1; block[6*BLOCK+col]=t1-p1;
        block[2*BLOCK+col]=t2+p2; block[5*BLOCK+col]=t2-p2;
        block[3*BLOCK+col]=t3+p3; block[4*BLOCK+col]=t3-p3;
    }

    for (int row = 0; row < BLOCK; row++) {
        float* b = block + row * BLOCK;
        float b0=b[0]*C4, b4=b[4]*C4,
              b1=b[1], b2=b[2], b3=b[3], b5=b[5], b6=b[6], b7=b[7];
        float s0=b0+b4, s1=b0-b4,
              s2=b2*C6-b6*C2, s3=b2*C2+b6*C6;
        float t0=s0+s3, t1=s1+s2, t2=s1-s2, t3=s0-s3;
        float p0=b1*C1+b3*C3+b5*C5+b7*C7,
              p1=b1*C3-b3*C7-b5*C1-b7*C5,
              p2=b1*C5-b3*C1+b5*C7+b7*C3,
              p3=b1*C7-b3*C5+b5*C3-b7*C1;
        b[0]=t0+p0; b[7]=t0-p0;
        b[1]=t1+p1; b[6]=t1-p1;
        b[2]=t2+p2; b[5]=t2-p2;
        b[3]=t3+p3; b[4]=t3-p3;
    }
}

// ── Channel helpers ───────────────────────────────────────────────────────────

// Compress one float channel plane → Huffman-coded coefficient byte stream.
// Returns malloc'd buffer, sets *outLen.
static unsigned char* compressChannel(const float* plane, int w, int h,
                                       const int* qtable, int* outLen) {
    int blocksX     = (w + BLOCK - 1) / BLOCK;
    int blocksY     = (h + BLOCK - 1) / BLOCK;
    int totalBlocks = blocksX * blocksY;

    // Raw coeff buffer: 4-byte block count + per-block [2-byte count + 64×2-byte coeffs]
    int rawBufSize = 4 + totalBlocks * (2 + BLOCK2 * 2) + 16;
    unsigned char* rawBuf = (unsigned char*)malloc(rawBufSize);
    int rawPos = 0;

    // Write total block count
    rawBuf[rawPos++] = (unsigned char)( totalBlocks        & 0xFF);
    rawBuf[rawPos++] = (unsigned char)((totalBlocks >>  8) & 0xFF);
    rawBuf[rawPos++] = (unsigned char)((totalBlocks >> 16) & 0xFF);
    rawBuf[rawPos++] = (unsigned char)((totalBlocks >> 24) & 0xFF);

    float block[BLOCK2];
    int   qblock[BLOCK2], zblock[BLOCK2];

    for (int by = 0; by < blocksY; by++) {
        for (int bx = 0; bx < blocksX; bx++) {
            // Fill 8×8 block, padding at edges
            for (int r = 0; r < BLOCK; r++) {
                for (int c = 0; c < BLOCK; c++) {
                    int px = bx*BLOCK+c; if (px >= w) px = w-1;
                    int py = by*BLOCK+r; if (py >= h) py = h-1;
                    block[r*BLOCK+c] = plane[py*w+px] - 128.0f;
                }
            }

            forwardDCT(block);

            // Quantize then zigzag-scan into zblock
            for (int i = 0; i < BLOCK2; i++)
                qblock[i] = (int)(block[i] / (float)qtable[i]);
            for (int i = 0; i < BLOCK2; i++)
                zblock[i] = qblock[ZIGZAG[i]];

            // Write [count=64 LE u16][coeff0 LE i16]...[coeff63 LE i16]
            rawBuf[rawPos++] = (unsigned char)(BLOCK2 & 0xFF);
            rawBuf[rawPos++] = (unsigned char)((BLOCK2 >> 8) & 0xFF);
            for (int i = 0; i < BLOCK2; i++) {
                short v = (short)clamp(zblock[i], -32768, 32767);
                rawBuf[rawPos++] = (unsigned char)( v       & 0xFF);
                rawBuf[rawPos++] = (unsigned char)((v >> 8) & 0xFF);
            }
        }
    }

    // Hand the raw byte stream to huffmanCompressBuffer from huffman_internal.h
    int hufBufSize = HUFFMAN_HEADER_SIZE + rawPos + 8;
    unsigned char* hufBuf = (unsigned char*)malloc(hufBufSize);
    *outLen = huffmanCompressBuffer(rawBuf, rawPos, hufBuf);

    free(rawBuf);
    return hufBuf;
}

// Decompress one Huffman-coded channel blob → float plane[w*h].
// Caller must free the returned pointer.
static float* decompressChannel(const unsigned char* src, int srcLen,
                                  const int* qtable, int w, int h) {
    if (srcLen <= HUFFMAN_HEADER_SIZE) return nullptr;

    int blocksX = (w + BLOCK - 1) / BLOCK;
    int blocksY = (h + BLOCK - 1) / BLOCK;
    int totalBlocks = blocksX * blocksY;

    // Huffman decode → raw coeff bytes
    int rawBufSize = 4 + totalBlocks * (2 + BLOCK2 * 2) + 16;
    unsigned char* rawBuf = (unsigned char*)malloc(rawBufSize);
    int rawLen = huffmanDecompressBuffer(src, srcLen, rawBuf);
    if (rawLen <= 4) { free(rawBuf); return nullptr; }

    float* plane = (float*)malloc(w * h * sizeof(float));
    float block[BLOCK2];
    int   zblock[BLOCK2], qblock[BLOCK2];

    int rp = 4;  // skip 4-byte block count header

    for (int by = 0; by < blocksY && rp + 2 <= rawLen; by++) {
        for (int bx = 0; bx < blocksX && rp + 2 <= rawLen; bx++) {
            int n = (int)rawBuf[rp] | ((int)rawBuf[rp+1] << 8);
            rp += 2;
            if (n > BLOCK2) n = BLOCK2;

            for (int i = 0; i < n && rp + 2 <= rawLen; i++) {
                short v = (short)((int)rawBuf[rp] | ((int)rawBuf[rp+1] << 8));
                rp += 2;
                zblock[i] = (int)v;
            }
            for (int i = n; i < BLOCK2; i++) zblock[i] = 0;

            // Inverse zigzag
            for (int i = 0; i < BLOCK2; i++)
                qblock[ZIGZAG[i]] = zblock[i];

            // Dequantize
            for (int i = 0; i < BLOCK2; i++)
                block[i] = (float)(qblock[i] * qtable[i]);

            inverseDCT(block);

            // Write pixels back, skipping out-of-bounds
            for (int r = 0; r < BLOCK; r++) {
                for (int c = 0; c < BLOCK; c++) {
                    int px = bx*BLOCK+c, py = by*BLOCK+r;
                    if (px >= w || py >= h) continue;
                    plane[py*w+px] = block[r*BLOCK+c] + 128.0f;
                }
            }
        }
    }

    free(rawBuf);
    return plane;
}

// ── Exported WASM functions ───────────────────────────────────────────────────

extern "C" {

// dctCompress
//   input:   raw RGBA pixels, width * height * 4 bytes
//   output:  DCTZ bitstream (caller allocates: width*height*3 + 512 is safe)
//   quality: 1 (smallest/worst) – 100 (largest/best)
//   returns: bytes written, or 0 on error
EMSCRIPTEN_KEEPALIVE
int dctCompress(const unsigned char* input, unsigned char* output,
                int width, int height, int quality) {
    if (!input || !output || width <= 0 || height <= 0) return 0;
    quality = clamp(quality, 1, 100);

    int lumaQ[BLOCK2], chromaQ[BLOCK2];
    buildQuantTable(LUMA_QUANT,   quality, lumaQ);
    buildQuantTable(CHROMA_QUANT, quality, chromaQ);

    // Convert RGBA → three separate float planes
    int n = width * height;
    float* Y  = (float*)malloc(n * sizeof(float));
    float* Cb = (float*)malloc(n * sizeof(float));
    float* Cr = (float*)malloc(n * sizeof(float));
    if (!Y || !Cb || !Cr) { free(Y); free(Cb); free(Cr); return 0; }

    for (int i = 0; i < n; i++)
        rgbToYCbCr(input[i*4], input[i*4+1], input[i*4+2], &Y[i], &Cb[i], &Cr[i]);

    // Compress each channel independently
    int yLen=0, cbLen=0, crLen=0;
    unsigned char* yBuf  = compressChannel(Y,  width, height, lumaQ,   &yLen);
    unsigned char* cbBuf = compressChannel(Cb, width, height, chromaQ, &cbLen);
    unsigned char* crBuf = compressChannel(Cr, width, height, chromaQ, &crLen);
    free(Y); free(Cb); free(Cr);

    // Write wire format
    int p = 0;
    output[p++]='D'; output[p++]='C'; output[p++]='T'; output[p++]='Z';

    output[p++]=(unsigned char)( width&0xFF);        output[p++]=(unsigned char)((width>>8)&0xFF);
    output[p++]=(unsigned char)((width>>16)&0xFF);   output[p++]=(unsigned char)((width>>24)&0xFF);
    output[p++]=(unsigned char)( height&0xFF);       output[p++]=(unsigned char)((height>>8)&0xFF);
    output[p++]=(unsigned char)((height>>16)&0xFF);  output[p++]=(unsigned char)((height>>24)&0xFF);

    output[p++] = (unsigned char)quality;

    output[p++]=(unsigned char)( yLen&0xFF);  output[p++]=(unsigned char)((yLen>>8)&0xFF);
    output[p++]=(unsigned char)((yLen>>16)&0xFF); output[p++]=(unsigned char)((yLen>>24)&0xFF);
    output[p++]=(unsigned char)( cbLen&0xFF); output[p++]=(unsigned char)((cbLen>>8)&0xFF);
    output[p++]=(unsigned char)((cbLen>>16)&0xFF); output[p++]=(unsigned char)((cbLen>>24)&0xFF);
    output[p++]=(unsigned char)( crLen&0xFF); output[p++]=(unsigned char)((crLen>>8)&0xFF);
    output[p++]=(unsigned char)((crLen>>16)&0xFF); output[p++]=(unsigned char)((crLen>>24)&0xFF);

    memcpy(output+p, yBuf,  yLen);  p += yLen;
    memcpy(output+p, cbBuf, cbLen); p += cbLen;
    memcpy(output+p, crBuf, crLen); p += crLen;

    free(yBuf); free(cbBuf); free(crBuf);
    return p;
}

// dctDecompress
//   input:    DCTZ bitstream produced by dctCompress
//   output:   raw RGBA pixels (allocate inputLen*12 + 64 to be safe)
//   outWidth / outHeight: populated by this function
//   returns:  bytes written (width*height*4), or 0 on error
EMSCRIPTEN_KEEPALIVE
int dctDecompress(const unsigned char* input, unsigned char* output,
                  int inputLen, int* outWidth, int* outHeight) {
    if (inputLen < 29) return 0;

    int p = 0;
    if (input[p]!='D'||input[p+1]!='C'||input[p+2]!='T'||input[p+3]!='Z') return 0;
    p += 4;

    int width  = (int)input[p]|((int)input[p+1]<<8)|((int)input[p+2]<<16)|((int)input[p+3]<<24); p+=4;
    int height = (int)input[p]|((int)input[p+1]<<8)|((int)input[p+2]<<16)|((int)input[p+3]<<24); p+=4;
    int quality = (int)input[p++];

    int yLen  = (int)input[p]|((int)input[p+1]<<8)|((int)input[p+2]<<16)|((int)input[p+3]<<24); p+=4;
    int cbLen = (int)input[p]|((int)input[p+1]<<8)|((int)input[p+2]<<16)|((int)input[p+3]<<24); p+=4;
    int crLen = (int)input[p]|((int)input[p+1]<<8)|((int)input[p+2]<<16)|((int)input[p+3]<<24); p+=4;

    if (width<=0||height<=0||quality<1||quality>100) return 0;
    if (p + yLen + cbLen + crLen > inputLen) return 0;

    int lumaQ[BLOCK2], chromaQ[BLOCK2];
    buildQuantTable(LUMA_QUANT,   quality, lumaQ);
    buildQuantTable(CHROMA_QUANT, quality, chromaQ);

    float* Y  = decompressChannel(input+p, yLen,  lumaQ,   width, height); p += yLen;
    float* Cb = decompressChannel(input+p, cbLen, chromaQ, width, height); p += cbLen;
    float* Cr = decompressChannel(input+p, crLen, chromaQ, width, height);

    if (!Y || !Cb || !Cr) { free(Y); free(Cb); free(Cr); return 0; }

    int n = width * height;
    for (int i = 0; i < n; i++) {
        unsigned char r, g, b;
        yCbCrToRgb(Y[i], Cb[i], Cr[i], &r, &g, &b);
        output[i*4+0]=r; output[i*4+1]=g;
        output[i*4+2]=b; output[i*4+3]=255;
    }

    free(Y); free(Cb); free(Cr);
    *outWidth  = width;
    *outHeight = height;
    return n * 4;
}

} // extern "C"