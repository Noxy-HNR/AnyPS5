#include "prx/libSceFreeType/include/GuestFreeType.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_STROKER_H

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace FreeType;

extern "C" {
std::int32_t APS5_VABI FT_Outline_Get_Orientation_nid_postfix(GuestOutline*);
std::int32_t APS5_VABI FT_Outline_GetInsideBorder_nid_postfix(GuestOutline*);
std::int32_t APS5_VABI FT_Outline_GetOutsideBorder_nid_postfix(GuestOutline*);
}

static void Check(bool value, int line) {
    if (!value) {
        std::fprintf(stderr, "FreeType outline orientation check failed at line %d\n", line);
        std::abort();
    }
}

#define Require(value) Check((value), __LINE__)

template <typename TCall> static bool ThrowsWith(TCall call, const std::string& prefix) {
    try {
        call();
    } catch (const std::runtime_error& error) {
        return std::string(error.what()).rfind(prefix, 0) == 0;
    }
    return false;
}

static std::int32_t HighestBit(std::uint32_t value) {
    return value == 0 ? 0 : static_cast<std::int32_t>(std::bit_width(value) - 1);
}

static std::int64_t Scaled(std::int64_t value, std::int32_t shift) {
    return shift == 0 ? value : value >> shift;
}

static std::int64_t ControlAreaSum(const std::vector<GuestVector>& points, const std::vector<std::uint16_t>& contours) {
    std::int64_t xMin = points[0].x;
    std::int64_t xMax = points[0].x;
    std::int64_t yMin = points[0].y;
    std::int64_t yMax = points[0].y;
    for (const GuestVector& point : points) {
        xMin = std::min(xMin, point.x);
        xMax = std::max(xMax, point.x);
        yMin = std::min(yMin, point.y);
        yMax = std::max(yMax, point.y);
    }
    if (xMin == xMax || yMin == yMax) return 0;
    if (xMin < -0x1000000 || yMin < -0x1000000 || xMax > 0x1000000 || yMax > 0x1000000) return 0;
    const auto magnitude = static_cast<std::uint32_t>((xMax < 0 ? -xMax : xMax) | (xMin < 0 ? -xMin : xMin));
    const std::int32_t xShift = std::max(HighestBit(magnitude) - 14, 0);
    const std::int32_t yShift = std::max(HighestBit(static_cast<std::uint32_t>(yMax - yMin)) - 14, 0);
    std::int64_t total = 0;
    std::int32_t last = -1;
    for (const std::uint16_t contour : contours) {
        const std::int32_t first = last + 1;
        last = contour;
        for (std::int32_t index = first; index <= last; ++index) {
            const std::int32_t next = index == last ? first : index + 1;
            total += Scaled(points[index].x, xShift) * Scaled(points[next].y, yShift) - Scaled(points[next].x, xShift) * Scaled(points[index].y, yShift);
        }
    }
    return total;
}

static std::int32_t ExpectedOrientation(const std::vector<GuestVector>& points, const std::vector<std::uint16_t>& contours) {
    const std::int64_t total = ControlAreaSum(points, contours);
    if (total > 0) return FT_ORIENTATION_POSTSCRIPT;
    if (total < 0) return FT_ORIENTATION_TRUETYPE;
    return FT_ORIENTATION_NONE;
}

static std::int32_t NativeOrientation(const std::vector<GuestVector>& points, const std::vector<std::uint16_t>& contours) {
    std::vector<FT_Vector> hostPoints;
    hostPoints.reserve(points.size());
    for (const GuestVector& point : points) hostPoints.push_back({static_cast<FT_Pos>(point.x), static_cast<FT_Pos>(point.y)});
    std::vector<unsigned short> hostContours(contours.begin(), contours.end());
    std::vector<unsigned char> hostTags(points.size(), 1);
    FT_Outline outline{};
    outline.n_contours = static_cast<unsigned short>(hostContours.size());
    outline.n_points = static_cast<unsigned short>(hostPoints.size());
    outline.points = hostPoints.data();
    outline.tags = hostTags.data();
    outline.contours = hostContours.data();
    outline.flags = 0;
    return static_cast<std::int32_t>(FT_Outline_Get_Orientation(&outline));
}

static void RequireNativeAgreement(const std::vector<GuestVector>& points, const std::vector<std::uint16_t>& contours, std::int32_t expected) {
    const std::int64_t total = ControlAreaSum(points, contours);
    Require(total > -2147483647 && total < 2147483647);
    Require(NativeOrientation(points, contours) == expected);
}

struct GuestOutlineFixture {
    std::vector<GuestVector> points;
    std::vector<unsigned char> tags;
    std::vector<std::uint16_t> contours;
    GuestOutline outline{};

    void Load(std::vector<GuestVector> loadedPoints, std::vector<std::uint16_t> loadedContours) {
        points = std::move(loadedPoints);
        tags.assign(points.size(), 1);
        contours = std::move(loadedContours);
        outline = GuestOutline{};
        outline.n_contours = static_cast<std::uint16_t>(contours.size());
        outline.n_points = static_cast<std::uint16_t>(points.size());
        outline.points = points.empty() ? nullptr : points.data();
        outline.tags = tags.empty() ? nullptr : tags.data();
        outline.contours = contours.empty() ? nullptr : contours.data();
        outline.flags = 0;
    }
};

static void RequireOrientation(GuestOutline* outline, std::int32_t orientation) {
    Require(FT_Outline_Get_Orientation_nid_postfix(outline) == orientation);
    Require(FT_Outline_GetInsideBorder_nid_postfix(outline) == (orientation == FT_ORIENTATION_TRUETYPE ? FT_STROKER_BORDER_RIGHT : FT_STROKER_BORDER_LEFT));
    Require(FT_Outline_GetOutsideBorder_nid_postfix(outline) == (orientation == FT_ORIENTATION_TRUETYPE ? FT_STROKER_BORDER_LEFT : FT_STROKER_BORDER_RIGHT));
}

int main() {
    GuestOutlineFixture fixture;

    Require(FT_Outline_Get_Orientation_nid_postfix(nullptr) == FT_ORIENTATION_TRUETYPE);
    Require(FT_Outline_GetInsideBorder_nid_postfix(nullptr) == FT_STROKER_BORDER_RIGHT);
    Require(FT_Outline_GetOutsideBorder_nid_postfix(nullptr) == FT_STROKER_BORDER_LEFT);

    fixture.Load({}, {});
    Require(fixture.outline.n_points == 0 && fixture.outline.n_contours == 0);
    RequireOrientation(&fixture.outline, FT_ORIENTATION_TRUETYPE);

    const std::vector<GuestVector> clockwise{{0, 0}, {0, 100}, {100, 100}, {100, 0}};
    const std::vector<GuestVector> counterclockwise{{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    const std::vector<std::uint16_t> single{3};

    fixture.Load(clockwise, single);
    RequireOrientation(&fixture.outline, FT_ORIENTATION_TRUETYPE);
    RequireNativeAgreement(clockwise, single, FT_ORIENTATION_TRUETYPE);

    fixture.Load(counterclockwise, single);
    RequireOrientation(&fixture.outline, FT_ORIENTATION_POSTSCRIPT);
    RequireNativeAgreement(counterclockwise, single, FT_ORIENTATION_POSTSCRIPT);

    const std::vector<GuestVector> collinear{{0, 0}, {50, 50}, {100, 100}};
    fixture.Load(collinear, {2});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);
    RequireNativeAgreement(collinear, {2}, FT_ORIENTATION_NONE);

    fixture.Load({{0, 5}, {100, 5}}, {1});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);

    fixture.Load({{10, 10}}, {});
    Require(fixture.outline.n_points == 1 && fixture.outline.n_contours == 0);
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);

    fixture.Load({{10, 10}}, {0});
    Require(fixture.outline.n_points == 1 && fixture.outline.n_contours == 1);
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);
    fixture.outline.contours = nullptr;
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);
    FT_Vector nativePoint{10, 10};
    FT_Outline nativeCollapsed{};
    nativeCollapsed.n_points = 1;
    nativeCollapsed.n_contours = 1;
    nativeCollapsed.points = &nativePoint;
    Require(FT_Outline_Get_Orientation(&nativeCollapsed) == FT_ORIENTATION_NONE);
    Require(FT_Outline_GetInsideBorder(&nativeCollapsed) == FT_STROKER_BORDER_LEFT);
    Require(FT_Outline_GetOutsideBorder(&nativeCollapsed) == FT_STROKER_BORDER_RIGHT);

    const std::vector<GuestVector> innerClockwise{{25, 25}, {25, 75}, {75, 75}, {75, 25}};
    const std::vector<GuestVector> innerCounterclockwise{{25, 25}, {75, 25}, {75, 75}, {25, 75}};
    std::vector<GuestVector> hole = counterclockwise;
    hole.insert(hole.end(), innerClockwise.begin(), innerClockwise.end());
    fixture.Load(hole, {3, 7});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_POSTSCRIPT);
    RequireNativeAgreement(hole, {3, 7}, FT_ORIENTATION_POSTSCRIPT);

    hole = counterclockwise;
    hole.insert(hole.end(), innerCounterclockwise.begin(), innerCounterclockwise.end());
    fixture.Load(hole, {3, 7});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_POSTSCRIPT);

    hole = clockwise;
    hole.insert(hole.end(), innerCounterclockwise.begin(), innerCounterclockwise.end());
    fixture.Load(hole, {3, 7});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_TRUETYPE);

    hole = clockwise;
    hole.insert(hole.end(), innerClockwise.begin(), innerClockwise.end());
    fixture.Load(hole, {3, 7});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_TRUETYPE);

    fixture.Load(counterclockwise, {});
    Require(fixture.outline.n_points == 4 && fixture.outline.n_contours == 0);
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);

    fixture.Load(counterclockwise, single);
    fixture.outline.flags = 0x12345678;
    for (unsigned char& tag : fixture.tags) tag = 0xFE;
    RequireOrientation(&fixture.outline, FT_ORIENTATION_POSTSCRIPT);

    fixture.Load({{0, 0}, {0x1000000, 0}, {0x1000000, 0x1000000}, {0, 0x1000000}}, {3});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_POSTSCRIPT);

    fixture.Load({{0, 0}, {0x1000001, 0}, {0x1000001, 0x1000001}, {0, 0x1000001}}, {3});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);
    fixture.outline.contours = nullptr;
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);

    fixture.Load({{0, 0}, {std::numeric_limits<std::int64_t>::max(), 0}, {std::numeric_limits<std::int64_t>::max(), 1}, {0, 1}}, {3});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);

    fixture.Load({{0, 0}, {std::numeric_limits<std::int64_t>::min(), 0}, {std::numeric_limits<std::int64_t>::min(), 1}, {0, 1}}, {3});
    RequireOrientation(&fixture.outline, FT_ORIENTATION_NONE);

    const std::vector<GuestVector> wideX{{-100000, 0}, {100, 0}, {100, 100}, {-100000, 100}};
    fixture.Load(wideX, {3});
    Require(ExpectedOrientation(wideX, {3}) == FT_ORIENTATION_POSTSCRIPT);
    RequireOrientation(&fixture.outline, ExpectedOrientation(wideX, {3}));
    RequireNativeAgreement(wideX, {3}, FT_ORIENTATION_POSTSCRIPT);

    const std::vector<GuestVector> wideY{{0, -100000}, {100, -100000}, {100, 100000}, {0, 100000}};
    fixture.Load(wideY, {3});
    Require(ExpectedOrientation(wideY, {3}) == FT_ORIENTATION_POSTSCRIPT);
    RequireOrientation(&fixture.outline, ExpectedOrientation(wideY, {3}));
    RequireNativeAgreement(wideY, {3}, FT_ORIENTATION_POSTSCRIPT);

    const std::vector<GuestVector> large{{0, 0}, {100000, 0}, {100000, 100000}, {0, 100000}};
    fixture.Load(large, {3});
    Require(ExpectedOrientation(large, {3}) == FT_ORIENTATION_POSTSCRIPT);
    RequireOrientation(&fixture.outline, ExpectedOrientation(large, {3}));

    fixture.Load(counterclockwise, single);
    const GuestOutline untouched = fixture.outline;
    const std::vector<GuestVector> untouchedPoints = fixture.points;
    const std::vector<unsigned char> untouchedTags = fixture.tags;
    const std::vector<std::uint16_t> untouchedContours = fixture.contours;
    RequireOrientation(&fixture.outline, FT_ORIENTATION_POSTSCRIPT);
    Require(fixture.outline.n_contours == untouched.n_contours && fixture.outline.n_points == untouched.n_points);
    Require(fixture.outline.points == untouched.points && fixture.outline.tags == untouched.tags && fixture.outline.contours == untouched.contours);
    Require(fixture.outline.flags == untouched.flags);
    Require(fixture.points.size() == untouchedPoints.size());
    for (std::size_t index = 0; index < fixture.points.size(); ++index) {
        Require(fixture.points[index].x == untouchedPoints[index].x && fixture.points[index].y == untouchedPoints[index].y);
    }
    Require(fixture.tags == untouchedTags && fixture.contours == untouchedContours);

    GuestOutlineFixture broken;
    broken.Load(counterclockwise, single);
    broken.outline.points = nullptr;
    Require(ThrowsWith([&] { FT_Outline_Get_Orientation_nid_postfix(&broken.outline); },
                       "FT_Outline_Get_Orientation: an outline without a points array is not implemented"));
    Require(ThrowsWith([&] { FT_Outline_GetInsideBorder_nid_postfix(&broken.outline); },
                       "FT_Outline_Get_Orientation: an outline without a points array is not implemented"));

    broken.Load(counterclockwise, single);
    broken.outline.contours = nullptr;
    Require(ThrowsWith([&] { FT_Outline_Get_Orientation_nid_postfix(&broken.outline); },
                       "FT_Outline_Get_Orientation: an outline without a contours array is not implemented"));

    broken.Load(counterclockwise, single);
    broken.contours[0] = 4;
    Require(ThrowsWith([&] { FT_Outline_Get_Orientation_nid_postfix(&broken.outline); },
                       "FT_Outline_Get_Orientation: a contour that ends past the last point is not implemented"));
    Require(ThrowsWith([&] { FT_Outline_GetOutsideBorder_nid_postfix(&broken.outline); },
                       "FT_Outline_Get_Orientation: a contour that ends past the last point is not implemented"));

    std::vector<GuestVector> big;
    const std::int64_t bigLeft = -16777215;
    const std::int64_t bigRight = 16777215;
    const std::int64_t bigBottom = -1;
    const std::int64_t bigTop = 11719168;
    for (std::int64_t x = -16760832; x <= 16760832; x += 16384) big.push_back({x, bigBottom});
    big.push_back({bigRight, bigBottom});
    for (std::int64_t y = 0; y <= 11698176; y += 16384) big.push_back({bigRight, y});
    big.push_back({bigRight, bigTop});
    for (std::int64_t x = 16760832; x >= -16760832; x -= 16384) big.push_back({x, bigTop});
    big.push_back({bigLeft, bigTop});
    for (std::int64_t y = 11698176; y >= 0; y -= 16384) big.push_back({bigLeft, y});
    big.push_back({bigLeft, bigBottom});
    const std::vector<std::uint16_t> bigContours{static_cast<std::uint16_t>(big.size() - 1)};
    fixture.Load(big, bigContours);
    const std::int64_t bigTotal = ControlAreaSum(big, bigContours);
    Require(bigTotal == 3000192300LL);
    Require(bigTotal > 2147483647 && bigTotal < 4294967296);
    Require((static_cast<std::int32_t>(static_cast<std::uint32_t>(bigTotal)) > 0) != (bigTotal > 0));
    RequireOrientation(&fixture.outline, FT_ORIENTATION_POSTSCRIPT);

    std::vector<GuestVector> maximum;
    maximum.push_back({0, 0});
    for (std::int64_t x = 1; x <= 65532; ++x) maximum.push_back({x, 0});
    maximum.push_back({65534, 0});
    maximum.push_back({65534, 1});
    const std::vector<std::uint16_t> maximumContours{65534};
    fixture.Load(maximum, maximumContours);
    Require(fixture.outline.n_points == 65535 && fixture.outline.n_contours == 1);
    Require(ExpectedOrientation(maximum, maximumContours) == FT_ORIENTATION_POSTSCRIPT);
    RequireOrientation(&fixture.outline, FT_ORIENTATION_POSTSCRIPT);
    RequireNativeAgreement(maximum, maximumContours, FT_ORIENTATION_POSTSCRIPT);
}
