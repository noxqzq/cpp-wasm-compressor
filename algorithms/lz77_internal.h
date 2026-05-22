#pragma once

// =============================================================================
//  lz77_internal.h
//  Shared LZ77 machinery used by lz77.cpp and zstd.cpp.
//  Include this header in both translation units — do NOT compile it directly.
//
//  Wire format (token stream):
//    Each token is either:
//      Literal:  [0x00][byte]          — 2 bytes total
//      Match:    [length][dist_lo][dist_hi] where length >= 1 and high bit of
//                first byte is set: i.e. [0x80 | (length-1)][dist_lo][dist_hi]
//                length: 1..128, distance: 1..65535
//
//  Simpler scheme used here for clarity and correctness:
//    Each token starts with a flag byte:
//      0x00  → literal follows (1 byte)
//      0x01  → match follows: [len_minus1][dist_lo][dist_hi] (3 bytes)
// =============================================================================

#define LZ77_MIN_MATCH    3
#define LZ77_WINDOW_SIZE  32768   // was 4096
#define LZ77_MAX_MATCH    65535   // was 258

// Find the longest match in the sliding window.
// Returns match length (0 if no match found), sets *matchDist.
inline int lz77FindMatch(const unsigned char* input, int pos, int inputLen,
                         int* matchDist) {
    int bestLen  = 0;
    int bestDist = 0;

    int windowStart = pos - LZ77_WINDOW_SIZE;
    if (windowStart < 0) windowStart = 0;

    int maxMatch = inputLen - pos;
    if (maxMatch > LZ77_MAX_MATCH) maxMatch = LZ77_MAX_MATCH;

    for (int i = windowStart; i < pos; i++) {
        int len = 0;
        while (len < maxMatch && input[i + len] == input[pos + len]) {
            len++;
        }
        if (len > bestLen) {
            bestLen  = len;
            bestDist = pos - i;
        }
    }

    *matchDist = bestDist;
    return bestLen;
}

// Compress src into dst. Returns number of bytes written to dst.
// dst must be large enough: worst case is srcLen * 2 + 4 bytes.
inline int lz77CompressBuffer(const unsigned char* src, unsigned char* dst, int srcLen) {
    if (srcLen == 0) return 0;

    int p   = 0;  // write position in dst
    int pos = 0;  // read position in src

    while (pos < srcLen) {
        int dist = 0;
        int len  = lz77FindMatch(src, pos, srcLen, &dist);

        if (len >= LZ77_MIN_MATCH) {
            // Match token: [0x01][len-1][dist_lo][dist_hi]
            dst[p++] = 0x01;
            dst[p++] = (unsigned char)(len - 1);
            dst[p++] = (unsigned char)( dist       & 0xFF);
            dst[p++] = (unsigned char)((dist >> 8) & 0xFF);
            pos += len;
        } else {
            // Literal token: [0x00][byte]
            dst[p++] = 0x00;
            dst[p++] = src[pos++];
        }
    }

    return p;
}

// Decompress src (LZ77 token stream) into dst. Returns number of bytes written.
// dst must be large enough to hold the original data.
inline int lz77DecompressBuffer(const unsigned char* src, unsigned char* dst, int srcLen) {
    int p      = 0;  // read position in src
    int outPos = 0;  // write position in dst

    while (p < srcLen) {
        unsigned char flag = src[p++];

        if (flag == 0x00) {
            // Literal
            if (p >= srcLen) break;
            dst[outPos++] = src[p++];
        } else if (flag == 0x01) {
            // Match
            if (p + 2 >= srcLen) break;
            int len  = (int)src[p++] + 1;
            int dist = (int)src[p] | ((int)src[p+1] << 8);
            p += 2;

            if (dist == 0) break;  // invalid

            int start = outPos - dist;
            if (start < 0) break;  // invalid reference

            // Copy with possible overlap (forward byte-by-byte)
            for (int i = 0; i < len; i++) {
                dst[outPos] = dst[start + i];
                outPos++;
            }
        } else {
            // Unknown flag — skip to avoid infinite loop
            break;
        }
    }

    return outPos;
}