#include "prx/libSceJxr/libSceJxr.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

extern "C" {
const char* JxrBitIOPointersTarget_nid_no_patch(void* codec, void** contexts, std::size_t* bytes);
std::int32_t JxrSetBitIOPointers_nid_no_patch(void* codec);
}

extern "C" std::int32_t APS5_VABI setBitIOPointers_nid_postfix(void* codec) {
    void* contexts = nullptr;
    std::size_t bytes = 0;
    if (const char* error = JxrBitIOPointersTarget_nid_no_patch(codec, &contexts, &bytes))
        throw std::invalid_argument(std::string(__func__) + ": " + error);
    GuestArena::HostWrite write(contexts, bytes);
    if (!write.Open()) throw std::invalid_argument(std::string(__func__) + ": coding context is not writable");
    return JxrSetBitIOPointers_nid_no_patch(codec);
}
