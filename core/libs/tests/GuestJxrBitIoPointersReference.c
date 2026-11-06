#include "strcodec.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { JxrPointerContexts = 16, JxrPointerTable = 96 };

typedef struct JxrPointerCase {
    CWMImageStrCodec codec;
    CCodingContext contexts[JxrPointerContexts];
    BitIOInfo* table[JxrPointerTable];
} JxrPointerCase;

static uint32_t Next(uint32_t* seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return *seed >> 8;
}

static void Fill(void* data, size_t bytes, uint32_t* seed) {
    unsigned char* out = (unsigned char*)data;
    size_t i;
    for (i = 0; i < bytes; ++i) out[i] = (unsigned char)Next(seed);
}

void* JxrPointersMake_nid_no_patch(uint32_t seed, uint32_t tiles, int32_t spatial, uint32_t subbands, size_t count) {
    JxrPointerCase* value = (JxrPointerCase*)malloc(sizeof(JxrPointerCase));
    size_t i;
    if (value == NULL) return NULL;
    Fill(value, sizeof(JxrPointerCase), &seed);
    for (i = 0; i < JxrPointerTable; ++i)
        value->table[i] = (BitIOInfo*)(uintptr_t)(0x100000u + 0x40u * i + (Next(&seed) & 0x3fu));
    value->codec.pIOHeader = (BitIOInfo*)(uintptr_t)0x7f0000u;
    value->codec.WMISCP.cNumOfSliceMinus1V = tiles - 1;
    value->codec.WMISCP.bfBitstreamFormat = spatial ? SPATIAL : FREQUENCY;
    value->codec.cSB = (U8)subbands;
    value->codec.cNumBitIO = count;
    value->codec.m_ppBitIO = value->table;
    value->codec.m_pCodingContext = value->contexts;
    return value;
}

void* JxrPointersClone_nid_no_patch(const void* source) {
    const JxrPointerCase* from = (const JxrPointerCase*)source;
    JxrPointerCase* to = (JxrPointerCase*)malloc(sizeof(JxrPointerCase));
    if (to == NULL) return NULL;
    memcpy(to, from, sizeof(JxrPointerCase));
    if (from->codec.m_ppBitIO == from->table) to->codec.m_ppBitIO = to->table;
    if (from->codec.m_pCodingContext == from->contexts) to->codec.m_pCodingContext = to->contexts;
    return to;
}

int JxrPointersEqual_nid_no_patch(const void* left, const void* right) {
    const JxrPointerCase* a = (const JxrPointerCase*)left;
    const JxrPointerCase* b = (const JxrPointerCase*)right;
    CWMImageStrCodec codecA = a->codec, codecB = b->codec;
    codecA.m_ppBitIO = codecB.m_ppBitIO = NULL;
    codecA.m_pCodingContext = codecB.m_pCodingContext = NULL;
    return memcmp(&codecA, &codecB, sizeof(codecA)) == 0 && memcmp(a->contexts, b->contexts, sizeof(a->contexts)) == 0 &&
           memcmp(a->table, b->table, sizeof(a->table)) == 0;
}

void* JxrPointersCodec_nid_no_patch(void* value) { return &((JxrPointerCase*)value)->codec; }
void* JxrPointersContexts_nid_no_patch(void* value) { return ((JxrPointerCase*)value)->contexts; }
size_t JxrPointersContextBytes_nid_no_patch(void) { return sizeof(CCodingContext); }

void JxrPointersSetTable_nid_no_patch(void* value, void* table) { ((JxrPointerCase*)value)->codec.m_ppBitIO = (BitIOInfo**)table; }
void JxrPointersSetContexts_nid_no_patch(void* value, void* contexts) {
    ((JxrPointerCase*)value)->codec.m_pCodingContext = (CCodingContext*)contexts;
}
void JxrPointersSetTiles_nid_no_patch(void* value, uint32_t columnsMinusOne) {
    ((JxrPointerCase*)value)->codec.WMISCP.cNumOfSliceMinus1V = columnsMinusOne;
}

int32_t JxrPointersReference_nid_no_patch(void* value) {
    return setBitIOPointers(&((JxrPointerCase*)value)->codec);
}
