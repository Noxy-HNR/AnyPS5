#include "prx/libSceFreeType/include/GuestObjects.hpp"

#include <ft2build.h>
#include FT_GASP_H

#include <cstdint>

using namespace FreeType;

extern "C" {

int APS5_VABI FT_Get_Gasp_nid_postfix(GuestFaceRec* face, std::uint32_t ppem) {
    const GuestFace* owner = FaceOf(face);
    return FT_Get_Gasp(owner ? owner->host : nullptr, ppem);
}

}
