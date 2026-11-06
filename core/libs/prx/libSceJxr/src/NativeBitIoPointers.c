#include "strcodec.h"
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(CWMImageStrCodec) == 35944 && sizeof(CCodingContext) == 704, "codec and coding context sizes");
_Static_assert(offsetof(CWMImageStrCodec, WMISCP) == 128 &&
                   offsetof(CWMImageStrCodec, WMISCP.cNumOfSliceMinus1V) == 200 &&
                   offsetof(CWMImageStrCodec, WMISCP.bfBitstreamFormat) == 156 && offsetof(CWMImageStrCodec, cSB) == 34360 &&
                   offsetof(CWMImageStrCodec, pIOHeader) == 34376 && offsetof(CWMImageStrCodec, m_ppBitIO) == 34456 &&
                   offsetof(CWMImageStrCodec, cNumBitIO) == 34464 && offsetof(CWMImageStrCodec, m_pCodingContext) == 34480,
               "CWMImageStrCodec layout");
_Static_assert(offsetof(CCodingContext, m_pIODC) == 0 && offsetof(CCodingContext, m_pIOLP) == 8 &&
                   offsetof(CCodingContext, m_pIOAC) == 16 && offsetof(CCodingContext, m_pIOFL) == 24,
               "CCodingContext layout");

const char* JxrBitIOPointersTarget_nid_no_patch(void* value, void** contexts, size_t* bytes) {
    const CWMImageStrCodec* codec = (const CWMImageStrCodec*)value;
    size_t tiles, last;
    if (codec == NULL) return "null codec";
    if (codec->m_pCodingContext == NULL) return "null coding context table";
    *contexts = codec->m_pCodingContext;
    if (codec->cNumBitIO == 0) {
        *bytes = sizeof(CCodingContext);
        return NULL;
    }
    if (codec->WMISCP.cNumOfSliceMinus1V >= MAX_TILES) return "more than 4096 tile columns";
    if (codec->m_ppBitIO == NULL) return "null bit I/O table";
    tiles = (size_t)codec->WMISCP.cNumOfSliceMinus1V + 1;
    if (codec->WMISCP.bfBitstreamFormat == SPATIAL) {
        last = tiles - 1;
    } else {
        size_t subbands = codec->cSB;
        last = (tiles - 1) * subbands + (subbands > 4 ? 3 : (subbands ? subbands - 1 : 0));
    }
    if (last >= codec->cNumBitIO) return "bit I/O index past cNumBitIO";
    *bytes = tiles * sizeof(CCodingContext);
    return NULL;
}

int32_t JxrSetBitIOPointers_nid_no_patch(void* value) {
    return setBitIOPointers((CWMImageStrCodec*)value);
}
