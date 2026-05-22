#pragma once
#include <stdlib.h>

// =============================================================================
//  huffman_internal.h
//  Shared Huffman machinery used by huffman.cpp and zstd_inspired.cpp.
//  Include this header in both translation units — do NOT compile it directly.
// =============================================================================

#define MAX_SYMBOLS          256
#define HUFFMAN_HEADER_SIZE  (4 + MAX_SYMBOLS * 2)   // 516 bytes

// ---------------------------------------------------------------------------
// Tree node
// ---------------------------------------------------------------------------

struct HuffmanNode {
    unsigned char byte;
    int           frequency;
    HuffmanNode*  left;
    HuffmanNode*  right;
};

inline HuffmanNode* createNode(unsigned char byte, int freq) {
    HuffmanNode* n = (HuffmanNode*)malloc(sizeof(HuffmanNode));
    n->byte        = byte;
    n->frequency   = freq;
    n->left        = nullptr;
    n->right       = nullptr;
    return n;
}

inline void freeTree(HuffmanNode* node) {
    if (!node) return;
    freeTree(node->left);
    freeTree(node->right);
    free(node);
}

// ---------------------------------------------------------------------------
// Min-heap
// ---------------------------------------------------------------------------

struct MinHeap {
    HuffmanNode** data;
    int           size;
    int           capacity;
};

inline MinHeap* createHeap(int capacity) {
    MinHeap* h  = (MinHeap*)malloc(sizeof(MinHeap));
    h->data     = (HuffmanNode**)malloc(capacity * sizeof(HuffmanNode*));
    h->size     = 0;
    h->capacity = capacity;
    return h;
}

inline void freeHeap(MinHeap* heap) {
    free(heap->data);
    free(heap);
}

inline void heapSwap(MinHeap* heap, int a, int b) {
    HuffmanNode* tmp = heap->data[a];
    heap->data[a]    = heap->data[b];
    heap->data[b]    = tmp;
}

inline void heapPush(MinHeap* heap, HuffmanNode* node) {
    int i = heap->size++;
    heap->data[i] = node;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap->data[parent]->frequency <= heap->data[i]->frequency) break;
        heapSwap(heap, parent, i);
        i = parent;
    }
}

inline HuffmanNode* heapPop(MinHeap* heap) {
    HuffmanNode* min = heap->data[0];
    heap->data[0]    = heap->data[--heap->size];
    int i = 0;
    while (true) {
        int left     = 2 * i + 1;
        int right    = 2 * i + 2;
        int smallest = i;
        if (left  < heap->size && heap->data[left]->frequency  < heap->data[smallest]->frequency) smallest = left;
        if (right < heap->size && heap->data[right]->frequency < heap->data[smallest]->frequency) smallest = right;
        if (smallest == i) break;
        heapSwap(heap, i, smallest);
        i = smallest;
    }
    return min;
}

// ---------------------------------------------------------------------------
// Tree builder
// ---------------------------------------------------------------------------

inline void buildFrequencyTable(const unsigned char* input, int inputLen, int* frequencies) {
    for (int i = 0; i < MAX_SYMBOLS; i++) frequencies[i] = 0;
    for (int i = 0; i < inputLen;    i++) frequencies[input[i]]++;
}

inline HuffmanNode* buildHuffmanTree(int* frequencies) {
    int symbolCount = 0;
    for (int i = 0; i < MAX_SYMBOLS; i++) if (frequencies[i] > 0) symbolCount++;
    if (symbolCount == 0) return nullptr;

    MinHeap* heap = createHeap(symbolCount * 2);
    for (int i = 0; i < MAX_SYMBOLS; i++)
        if (frequencies[i] > 0)
            heapPush(heap, createNode((unsigned char)i, frequencies[i]));

    while (heap->size > 1) {
        HuffmanNode* left  = heapPop(heap);
        HuffmanNode* right = heapPop(heap);
        HuffmanNode* parent = createNode(0, left->frequency + right->frequency);
        parent->left  = left;
        parent->right = right;
        heapPush(heap, parent);
    }

    HuffmanNode* root = heapPop(heap);
    freeHeap(heap);
    return root;
}

// ---------------------------------------------------------------------------
// Code generation
// ---------------------------------------------------------------------------

inline void generateCodes(HuffmanNode* node, unsigned int code, int depth,
                           unsigned int* codes, int* codeLengths) {
    if (!node) return;
    if (!node->left && !node->right) {
        codes[node->byte]       = code;
        codeLengths[node->byte] = depth == 0 ? 1 : depth;
        return;
    }
    generateCodes(node->left,  code << 1,       depth + 1, codes, codeLengths);
    generateCodes(node->right, (code << 1) | 1, depth + 1, codes, codeLengths);
}

// ---------------------------------------------------------------------------
// Compress / decompress helpers (used by zstd_inspired.cpp)
// ---------------------------------------------------------------------------

// Huffman-compress src into dst.
// Writes: 4-byte original length + 512-byte freq table + bitstream.
// Returns number of bytes written.
inline int huffmanCompressBuffer(const unsigned char* src, int srcLen, unsigned char* dst) {
    int frequencies[MAX_SYMBOLS];
    buildFrequencyTable(src, srcLen, frequencies);

    int p = 0;
    dst[p++] = (unsigned char)( srcLen        & 0xFF);
    dst[p++] = (unsigned char)((srcLen >>  8) & 0xFF);
    dst[p++] = (unsigned char)((srcLen >> 16) & 0xFF);
    dst[p++] = (unsigned char)((srcLen >> 24) & 0xFF);

    for (int i = 0; i < MAX_SYMBOLS; i++) {
        int f = frequencies[i] > 0xFFFF ? 0xFFFF : frequencies[i];
        dst[p++] = (unsigned char)( f       & 0xFF);
        dst[p++] = (unsigned char)((f >> 8) & 0xFF);
    }

    if (srcLen == 0) return p;

    HuffmanNode* root = buildHuffmanTree(frequencies);
    if (!root) return p;

    unsigned int codes[MAX_SYMBOLS]       = {0};
    int          codeLengths[MAX_SYMBOLS] = {0};
    generateCodes(root, 0, 0, codes, codeLengths);
    freeTree(root);

    unsigned int bitBuffer = 0;
    int bitCount  = 0;
    for (int i = 0; i < srcLen; i++) {
        unsigned char b = src[i];
        bitBuffer = (bitBuffer << codeLengths[b]) | codes[b];
        bitCount += codeLengths[b];
        while (bitCount >= 8) {
            bitCount -= 8;
            dst[p++] = (unsigned char)((bitBuffer >> bitCount) & 0xFF);
        }
    }
    if (bitCount > 0)
        dst[p++] = (unsigned char)((bitBuffer << (8 - bitCount)) & 0xFF);

    return p;
}

// Huffman-decompress src into dst. Returns decompressed byte count.
inline int huffmanDecompressBuffer(const unsigned char* src, int srcLen, unsigned char* dst) {
    if (srcLen <= HUFFMAN_HEADER_SIZE) return 0;

    int p = 0;
    int originalLen = (int)src[p]
                    | ((int)src[p+1] <<  8)
                    | ((int)src[p+2] << 16)
                    | ((int)src[p+3] << 24);
    p += 4;

    int frequencies[MAX_SYMBOLS];
    for (int i = 0; i < MAX_SYMBOLS; i++) {
        frequencies[i] = (int)src[p] | ((int)src[p+1] << 8);
        p += 2;
    }

    HuffmanNode* root = buildHuffmanTree(frequencies);
    if (!root) return 0;

    int outPos = 0;
    if (!root->left && !root->right) {
        for (int i = 0; i < originalLen; i++) dst[outPos++] = root->byte;
        freeTree(root);
        return outPos;
    }

    HuffmanNode* current = root;
    for (int byteIdx = p; byteIdx < srcLen && outPos < originalLen; byteIdx++) {
        unsigned char b = src[byteIdx];
        for (int bit = 7; bit >= 0 && outPos < originalLen; bit--) {
            current = (b >> bit) & 1 ? current->right : current->left;
            if (!current) break;
            if (!current->left && !current->right) {
                dst[outPos++] = current->byte;
                current = root;
            }
        }
    }

    freeTree(root);
    return outPos;
}