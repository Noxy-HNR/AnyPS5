#include "prx/libSceJxr/include/Api.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libkernel/libkernel.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
void* JxrPointersMake_nid_no_patch(std::uint32_t seed, std::uint32_t tiles, std::int32_t spatial, std::uint32_t subbands,
                                   std::size_t count);
void* JxrPointersClone_nid_no_patch(const void* source);
int JxrPointersEqual_nid_no_patch(const void* left, const void* right);
void* JxrPointersCodec_nid_no_patch(void* value);
void* JxrPointersContexts_nid_no_patch(void* value);
std::size_t JxrPointersContextBytes_nid_no_patch();
void JxrPointersSetTable_nid_no_patch(void* value, void* table);
void JxrPointersSetContexts_nid_no_patch(void* value, void* contexts);
void JxrPointersSetTiles_nid_no_patch(void* value, std::uint32_t columnsMinusOne);
std::int32_t JxrPointersReference_nid_no_patch(void* value);
}

namespace {

constexpr std::size_t Contexts = 16;
constexpr std::size_t Table = 96;

void Require(bool value, const char* message) {
    if (value) return;
    std::fprintf(stderr, "JXR bit I/O pointers: %s\n", message);
    std::abort();
}

template<typename TAction>
void RequireInvalidArgument(TAction action, const char* message) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    Require(false, message);
}

struct Pair {
    void* guest;
    void* native;
    Pair(std::uint32_t seed, std::uint32_t tiles, bool spatial, std::uint32_t subbands, std::size_t count) {
        guest = JxrPointersMake_nid_no_patch(seed, tiles, spatial, subbands, count);
        Require(guest != nullptr, "allocation");
        native = JxrPointersClone_nid_no_patch(guest);
        Require(native != nullptr, "allocation");
    }
    ~Pair() {
        std::free(guest);
        std::free(native);
    }
    Pair(const Pair&) = delete;
    Pair& operator=(const Pair&) = delete;

    void Run(const char* message) const {
        const std::int32_t expected = JxrPointersReference_nid_no_patch(native);
        Require(setBitIOPointers_nid_postfix(JxrPointersCodec_nid_no_patch(guest)) == expected, message);
        Require(JxrPointersEqual_nid_no_patch(guest, native), message);
    }

    void Reject(const char* message) const {
        RequireInvalidArgument([&] { setBitIOPointers_nid_postfix(JxrPointersCodec_nid_no_patch(guest)); }, message);
        Require(JxrPointersEqual_nid_no_patch(guest, native), message);
    }
};

std::size_t LastIndex(std::uint32_t tiles, bool spatial, std::uint32_t subbands) {
    if (spatial) return tiles - 1;
    const std::size_t extra = subbands > 4 ? 3 : (subbands ? subbands - 1 : 0);
    return std::size_t{tiles - 1} * subbands + extra;
}

void TestMatchesJxrlib() {
    std::uint32_t seed = 1;
    for (std::uint32_t tiles = 1; tiles <= Contexts; ++tiles) {
        for (std::uint32_t subbands = 0; subbands <= 6; ++subbands) {
            for (const bool spatial : {true, false}) {
                const std::size_t needed = LastIndex(tiles, spatial, subbands) + 1;
                for (const std::size_t count : {needed, needed + 1, Table}) {
                    if (count > Table) continue;
                    Pair pair(++seed, tiles, spatial, subbands, count);
                    pair.Run("setBitIOPointers differs from jxrlib");
                    pair.Run("second setBitIOPointers differs from jxrlib");
                }
            }
        }
    }
    for (std::uint32_t tiles = 1; tiles <= Contexts; tiles += 5) {
        Pair pair(++seed, tiles, false, 3, 0);
        pair.Run("streaming setBitIOPointers differs from jxrlib");
    }
}

void TestRejections() {
    RequireInvalidArgument([] { setBitIOPointers_nid_postfix(nullptr); }, "null codec");
    {
        Pair pair(100, 2, true, 1, 4);
        JxrPointersSetContexts_nid_no_patch(pair.guest, nullptr);
        JxrPointersSetContexts_nid_no_patch(pair.native, nullptr);
        pair.Reject("null coding context table");
    }
    {
        Pair pair(101, 2, false, 1, 0);
        JxrPointersSetContexts_nid_no_patch(pair.guest, nullptr);
        JxrPointersSetContexts_nid_no_patch(pair.native, nullptr);
        pair.Reject("null coding context table in streaming mode");
    }
    {
        Pair pair(102, 2, true, 1, 4);
        JxrPointersSetTable_nid_no_patch(pair.guest, nullptr);
        JxrPointersSetTable_nid_no_patch(pair.native, nullptr);
        pair.Reject("null bit I/O table");
    }
    {
        Pair pair(103, 2, true, 1, 4);
        JxrPointersSetTiles_nid_no_patch(pair.guest, 4096);
        JxrPointersSetTiles_nid_no_patch(pair.native, 4096);
        pair.Reject("more than 4096 tile columns");
    }
    for (std::uint32_t tiles = 1; tiles <= Contexts; tiles += 3) {
        for (std::uint32_t subbands = 0; subbands <= 6; ++subbands) {
            for (const bool spatial : {true, false}) {
                const std::size_t last = LastIndex(tiles, spatial, subbands);
                if (last == 0) continue;
                Pair pair(200 + tiles * 16 + subbands, tiles, spatial, subbands, last);
                pair.Reject("bit I/O index past cNumBitIO");
            }
        }
    }
}

#ifdef _WIN32
class GuestPages {
public:
    static constexpr std::size_t Page = 0x4000;
    static constexpr std::size_t Pages = 2;

    GuestPages() {
        Require(sceKernelAllocateDirectMemory(0, 0x7fffffffffll, Page * Pages, 0, 0, &physical) == 0, "allocate");
        Require(sceKernelMapDirectMemory(&mapping, Page * Pages, 3, 0, physical, 0) == 0, "map");
        Require(GuestArena::GuestArenaContains_nid_postfix(mapping, Page * Pages), "guest mapping");
    }

    ~GuestPages() {
        Require(sceKernelMunmap(reinterpret_cast<std::uint64_t>(mapping), Page * Pages) == 0, "unmap");
        Require(sceKernelReleaseDirectMemory(physical, Page * Pages) == 0, "release");
    }

    unsigned char* At(std::size_t page) const { return static_cast<unsigned char*>(mapping) + Page * page; }

    void Protect(std::size_t first, std::size_t count, int protection) const {
        Require(sceKernelMprotect(At(first), Page * count, protection) == 0, "protect");
        if (protection != 1) return;
        MEMORY_BASIC_INFORMATION information{};
        Require(VirtualQuery(At(first), &information, sizeof(information)) == sizeof(information), "query");
        Require(information.Protect == PAGE_READONLY, "read-only page");
    }

private:
    std::int64_t physical = 0;
    void* mapping = nullptr;
};

void TestReadOnlyContexts() {
    const std::size_t bytes = JxrPointersContextBytes_nid_no_patch() * Contexts;
    Require(bytes <= GuestPages::Page, "contexts fit one page");
    GuestPages pages;
    for (const std::size_t page : {std::size_t{0}, std::size_t{1}}) {
        Pair pair(300 + static_cast<std::uint32_t>(page), Contexts, false, 4, Table);
        unsigned char* contexts = pages.At(page) + (page ? 0 : GuestPages::Page - bytes);
        std::memcpy(contexts, JxrPointersContexts_nid_no_patch(pair.guest), bytes);
        JxrPointersSetContexts_nid_no_patch(pair.guest, contexts);
        pages.Protect(page, 1, 1);
        const std::vector<unsigned char> before(contexts, contexts + bytes);
        RequireInvalidArgument([&] { setBitIOPointers_nid_postfix(JxrPointersCodec_nid_no_patch(pair.guest)); },
                               "read-only coding contexts");
        Require(std::memcmp(contexts, before.data(), bytes) == 0, "read-only coding contexts unchanged");
        pages.Protect(page, 1, 3);
        Require(setBitIOPointers_nid_postfix(JxrPointersCodec_nid_no_patch(pair.guest)) ==
                    JxrPointersReference_nid_no_patch(pair.native),
                "setBitIOPointers after write access is restored");
        Require(std::memcmp(contexts, JxrPointersContexts_nid_no_patch(pair.native), bytes) == 0,
                "contexts after write access is restored");
    }
    {
        Pair pair(310, 1, false, 2, 0);
        unsigned char* context = pages.At(0);
        std::memcpy(context, JxrPointersContexts_nid_no_patch(pair.guest), JxrPointersContextBytes_nid_no_patch());
        JxrPointersSetContexts_nid_no_patch(pair.guest, context);
        pages.Protect(0, 1, 1);
        RequireInvalidArgument([&] { setBitIOPointers_nid_postfix(JxrPointersCodec_nid_no_patch(pair.guest)); },
                               "read-only streaming context");
        pages.Protect(0, 1, 3);
    }
}
#endif

} // namespace

int main() {
    TestMatchesJxrlib();
    TestRejections();
#ifdef _WIN32
    TestReadOnlyContexts();
#endif
    return 0;
}
