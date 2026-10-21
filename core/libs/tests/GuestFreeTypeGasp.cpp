#include "FreeTypeFontFixture.hpp"
#include "prx/libSceFreeType/include/GuestFreeType.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GASP_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

using namespace FreeType;

extern "C" {
int APS5_VABI FT_Init_FreeType_nid_postfix(void**);
int APS5_VABI FT_Done_FreeType_nid_postfix(void*);
int APS5_VABI FT_New_Memory_Face_nid_postfix(void*, const unsigned char*, std::int64_t, std::int64_t, GuestFaceRec**);
int APS5_VABI FT_Done_Face_nid_postfix(GuestFaceRec*);
int APS5_VABI FT_Set_Pixel_Sizes_nid_postfix(GuestFaceRec*, std::uint32_t, std::uint32_t);
int APS5_VABI FT_Load_Glyph_nid_postfix(GuestFaceRec*, std::uint32_t, std::int32_t);
int APS5_VABI FT_Get_Gasp_nid_postfix(GuestFaceRec*, std::uint32_t);
}

static void Check(bool value, int line) {
    if (!value) {
        std::fprintf(stderr, "FreeType gasp check failed at line %d\n", line);
        std::abort();
    }
}

#define Require(value) Check((value), __LINE__)

static std::uint16_t Read16(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8) | bytes[offset + 1]);
}

static std::uint32_t Read32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(Read16(bytes, offset)) << 16) | Read16(bytes, offset + 2);
}

struct GaspRange {
    std::uint16_t maxPPEM;
    std::uint16_t flags;
};

struct SfntTable {
    std::array<unsigned char, 4> tag{};
    std::vector<unsigned char> data;
};

static std::uint32_t Checksum(const std::vector<unsigned char>& bytes) {
    std::uint32_t sum = 0;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
        std::uint32_t word = 0;
        for (std::size_t byte = 0; byte < 4; ++byte) {
            word <<= 8;
            if (offset + byte < bytes.size()) word |= bytes[offset + byte];
        }
        sum += word;
    }
    return sum;
}

static std::vector<unsigned char> WithGasp(const std::vector<unsigned char>& base, std::uint16_t version, const std::vector<GaspRange>& ranges) {
    const std::uint16_t baseCount = Read16(base, 4);
    std::vector<SfntTable> tables;
    tables.reserve(static_cast<std::size_t>(baseCount) + 1);
    for (std::uint16_t index = 0; index < baseCount; ++index) {
        const std::size_t entry = 12 + static_cast<std::size_t>(index) * 16;
        SfntTable table;
        std::memcpy(table.tag.data(), base.data() + entry, 4);
        const std::uint32_t offset = Read32(base, entry + 8);
        const std::uint32_t length = Read32(base, entry + 12);
        Require(offset <= base.size() && length <= base.size() - offset);
        table.data.assign(base.begin() + static_cast<std::ptrdiff_t>(offset), base.begin() + static_cast<std::ptrdiff_t>(offset + length));
        tables.push_back(std::move(table));
    }
    SfntTable gasp;
    gasp.tag = {'g', 'a', 's', 'p'};
    Put16(gasp.data, version);
    Put16(gasp.data, static_cast<std::uint32_t>(ranges.size()));
    for (const auto& range : ranges) {
        Put16(gasp.data, range.maxPPEM);
        Put16(gasp.data, range.flags);
    }
    const auto position = std::find_if(tables.begin(), tables.end(), [](const SfntTable& table) {
        return std::memcmp(table.tag.data(), "gasp", 4) > 0;
    });
    tables.insert(position, std::move(gasp));

    const std::uint16_t tableCount = static_cast<std::uint16_t>(tables.size());
    std::uint16_t entrySelector = 0;
    while ((1u << (entrySelector + 1)) <= tableCount) ++entrySelector;
    const std::uint16_t searchRange = static_cast<std::uint16_t>((1u << entrySelector) * 16u);
    const std::uint16_t rangeShift = static_cast<std::uint16_t>(tableCount * 16u - searchRange);

    std::vector<std::uint32_t> checksums;
    checksums.reserve(tables.size());
    for (const auto& table : tables) checksums.push_back(Checksum(table.data));

    std::vector<unsigned char> font;
    Put32(font, 0x00010000u);
    Put16(font, tableCount);
    Put16(font, searchRange);
    Put16(font, entrySelector);
    Put16(font, rangeShift);
    std::vector<std::uint32_t> offsets;
    offsets.reserve(tables.size());
    std::uint32_t offset = 12 + static_cast<std::uint32_t>(tableCount) * 16;
    for (std::size_t index = 0; index < tables.size(); ++index) {
        offsets.push_back(offset);
        font.insert(font.end(), tables[index].tag.begin(), tables[index].tag.end());
        Put32(font, checksums[index]);
        Put32(font, offset);
        Put32(font, static_cast<std::uint32_t>(tables[index].data.size()));
        offset += (static_cast<std::uint32_t>(tables[index].data.size()) + 3u) & ~3u;
    }
    for (const auto& table : tables) {
        font.insert(font.end(), table.data.begin(), table.data.end());
        font.resize((font.size() + 3u) & ~std::size_t{3});
    }
    Require(font.size() == offset);
    for (std::size_t index = 0; index < tables.size(); ++index) {
        if (tables[index].tag != std::array<unsigned char, 4>{'h', 'e', 'a', 'd'}) continue;
        const std::uint32_t adjustment = 0xB1B0AFBAu - Checksum(font);
        for (std::size_t byte = 0; byte < 4; ++byte) font[offsets[index] + 8 + byte] = static_cast<unsigned char>(adjustment >> (24 - byte * 8));
    }
    return font;
}

struct Sample {
    std::uint32_t ppem;
    int flags;
};

static void RequireGasp(GuestFaceRec* guest, FT_Face native, std::initializer_list<Sample> samples) {
    for (const auto& sample : samples) {
        Require(FT_Get_Gasp_nid_postfix(guest, sample.ppem) == sample.flags);
        Require(FT_Get_Gasp(native, static_cast<FT_UInt>(sample.ppem)) == sample.flags);
    }
}

struct GuestState {
    std::array<unsigned char, sizeof(GuestFaceRec)> face{};
    std::array<unsigned char, sizeof(GuestSizeRec)> size{};
    std::array<unsigned char, sizeof(GuestGlyphSlotRec)> slot{};
    std::array<unsigned char, sizeof(GuestOutline)> outline{};
    std::vector<GuestVector> points;
    std::vector<unsigned char> tags;
    std::vector<std::uint16_t> contours;
    std::vector<GuestCharMapRec> charmaps;
};

static GuestState Capture(GuestFaceRec* face) {
    GuestState state;
    std::memcpy(state.face.data(), face, state.face.size());
    std::memcpy(state.size.data(), face->size, state.size.size());
    std::memcpy(state.slot.data(), face->glyph, state.slot.size());
    const auto& outline = face->glyph->outline;
    std::memcpy(state.outline.data(), &outline, state.outline.size());
    state.points.assign(outline.points, outline.points + outline.n_points);
    state.tags.assign(outline.tags, outline.tags + outline.n_points);
    state.contours.assign(outline.contours, outline.contours + outline.n_contours);
    for (int index = 0; index < face->num_charmaps; ++index) state.charmaps.push_back(*face->charmaps[index]);
    return state;
}

static void RequireSameState(const GuestState& expected, GuestFaceRec* face) {
    Require(std::memcmp(expected.face.data(), face, expected.face.size()) == 0);
    Require(std::memcmp(expected.size.data(), face->size, expected.size.size()) == 0);
    Require(std::memcmp(expected.slot.data(), face->glyph, expected.slot.size()) == 0);
    const auto& outline = face->glyph->outline;
    Require(std::memcmp(expected.outline.data(), &outline, expected.outline.size()) == 0);
    Require(expected.points.size() == outline.n_points);
    for (std::size_t index = 0; index < expected.points.size(); ++index) {
        Require(expected.points[index].x == outline.points[index].x && expected.points[index].y == outline.points[index].y);
        Require(expected.tags[index] == outline.tags[index]);
    }
    Require(expected.contours.size() == outline.n_contours);
    for (std::size_t index = 0; index < expected.contours.size(); ++index) Require(expected.contours[index] == outline.contours[index]);
    Require(expected.charmaps.size() == static_cast<std::size_t>(face->num_charmaps));
    for (std::size_t index = 0; index < expected.charmaps.size(); ++index) {
        Require(std::memcmp(&expected.charmaps[index], face->charmaps[index], sizeof(GuestCharMapRec)) == 0);
    }
}

int main() {
    const auto base = SquareGlyphFont('A', 700);
    const auto version0 = WithGasp(base, 0, {{12, 0x000F}, {24, 0x000C}, {65535, 0x000A}});
    const auto version1 = WithGasp(base, 1, {{10, 0x0004}, {22, 0x000C}, {40, 0x0008}});

    FT_Library nativeLibrary = nullptr;
    Require(FT_Init_FreeType(&nativeLibrary) == 0);
    void* library = nullptr;
    Require(FT_Init_FreeType_nid_postfix(&library) == 0);

    FT_Face native0 = nullptr;
    FT_Face native1 = nullptr;
    FT_Face nativeMissing = nullptr;
    Require(FT_New_Memory_Face(nativeLibrary, version0.data(), static_cast<FT_Long>(version0.size()), 0, &native0) == 0);
    Require(FT_New_Memory_Face(nativeLibrary, version1.data(), static_cast<FT_Long>(version1.size()), 0, &native1) == 0);
    Require(FT_New_Memory_Face(nativeLibrary, base.data(), static_cast<FT_Long>(base.size()), 0, &nativeMissing) == 0);

    GuestFaceRec* face0 = nullptr;
    GuestFaceRec* face1 = nullptr;
    GuestFaceRec* missing = nullptr;
    Require(FT_New_Memory_Face_nid_postfix(library, version0.data(), static_cast<std::int64_t>(version0.size()), 0, &face0) == 0);
    Require(FT_New_Memory_Face_nid_postfix(library, version1.data(), static_cast<std::int64_t>(version1.size()), 0, &face1) == 0);
    Require(FT_New_Memory_Face_nid_postfix(library, base.data(), static_cast<std::int64_t>(base.size()), 0, &missing) == 0);

    RequireGasp(face0, native0, {
        {0, FT_GASP_DO_GRIDFIT | FT_GASP_DO_GRAY},
        {11, FT_GASP_DO_GRIDFIT | FT_GASP_DO_GRAY},
        {12, FT_GASP_DO_GRIDFIT | FT_GASP_DO_GRAY},
        {13, 0},
        {23, 0},
        {24, 0},
        {25, FT_GASP_DO_GRAY},
        {47, FT_GASP_DO_GRAY},
        {48, FT_GASP_DO_GRAY},
        {49, FT_GASP_DO_GRAY},
        {65534, FT_GASP_DO_GRAY},
        {65535, FT_GASP_DO_GRAY},
        {std::numeric_limits<std::uint32_t>::max(), FT_GASP_NO_TABLE},
    });
    RequireGasp(face1, native1, {
        {0, FT_GASP_SYMMETRIC_GRIDFIT},
        {9, FT_GASP_SYMMETRIC_GRIDFIT},
        {10, FT_GASP_SYMMETRIC_GRIDFIT},
        {11, FT_GASP_SYMMETRIC_GRIDFIT | FT_GASP_SYMMETRIC_SMOOTHING},
        {21, FT_GASP_SYMMETRIC_GRIDFIT | FT_GASP_SYMMETRIC_SMOOTHING},
        {22, FT_GASP_SYMMETRIC_GRIDFIT | FT_GASP_SYMMETRIC_SMOOTHING},
        {23, FT_GASP_SYMMETRIC_SMOOTHING},
        {39, FT_GASP_SYMMETRIC_SMOOTHING},
        {40, FT_GASP_SYMMETRIC_SMOOTHING},
        {41, FT_GASP_NO_TABLE},
        {65535, FT_GASP_NO_TABLE},
        {std::numeric_limits<std::uint32_t>::max(), FT_GASP_NO_TABLE},
    });
    RequireGasp(missing, nativeMissing, {
        {0, FT_GASP_NO_TABLE},
        {12, FT_GASP_NO_TABLE},
        {65535, FT_GASP_NO_TABLE},
        {std::numeric_limits<std::uint32_t>::max(), FT_GASP_NO_TABLE},
    });

    Require(FT_Get_Gasp_nid_postfix(face0, 5) == (FT_GASP_DO_GRIDFIT | FT_GASP_DO_GRAY));
    Require(FT_Get_Gasp_nid_postfix(face1, 5) == FT_GASP_SYMMETRIC_GRIDFIT);
    Require(FT_Get_Gasp_nid_postfix(face0, 5) == (FT_GASP_DO_GRIDFIT | FT_GASP_DO_GRAY));
    Require(FT_Get_Gasp_nid_postfix(face1, 30) == FT_GASP_SYMMETRIC_SMOOTHING);
    Require(FT_Get_Gasp_nid_postfix(face0, 30) == FT_GASP_DO_GRAY);
    Require(FT_Get_Gasp_nid_postfix(face1, 30) == FT_GASP_SYMMETRIC_SMOOTHING);

    Require(FT_Get_Gasp_nid_postfix(nullptr, 12) == FT_GASP_NO_TABLE);
    Require(FT_Get_Gasp_nid_postfix(nullptr, std::numeric_limits<std::uint32_t>::max()) == FT_GASP_NO_TABLE);
    GuestFaceRec zeroed{};
    Require(FT_Get_Gasp_nid_postfix(&zeroed, 12) == FT_GASP_NO_TABLE);

    Require(FT_Set_Pixel_Sizes_nid_postfix(face1, 0, 100) == 0);
    Require(FT_Load_Glyph_nid_postfix(face1, 1, FT_LOAD_NO_SCALE) == 0);
    Require(face1->glyph->outline.n_points == 4);
    const std::int64_t changedX = face1->glyph->outline.points[0].x + 37;
    face1->glyph->outline.points[0].x = changedX;
    const GuestState state = Capture(face1);
    for (const std::uint32_t ppem : std::array<std::uint32_t, 6>{0, 10, 11, 41, 65535, std::numeric_limits<std::uint32_t>::max()}) {
        Require(FT_Get_Gasp_nid_postfix(face1, ppem) == FT_Get_Gasp(native1, static_cast<FT_UInt>(ppem)));
        RequireSameState(state, face1);
    }
    Require(face1->glyph->outline.points[0].x == changedX);

    Require(FT_Done_Face_nid_postfix(face0) == 0);
    Require(FT_Done_Face_nid_postfix(face1) == 0);
    Require(FT_Done_Face_nid_postfix(missing) == 0);
    Require(FT_Done_FreeType_nid_postfix(library) == 0);
    Require(FT_Done_Face(native0) == 0);
    Require(FT_Done_Face(native1) == 0);
    Require(FT_Done_Face(nativeMissing) == 0);
    Require(FT_Done_FreeType(nativeLibrary) == 0);
}
