#include "prx/libSceFreeType/include/GuestFreeType.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_STROKER_H

#include <algorithm>
#include <bit>
#include <cstdint>
#include <stdexcept>

using namespace FreeType;

namespace {

constexpr std::int64_t kCoordinateLimit = 0x1000000;
constexpr std::int32_t kShiftBase = 14;

struct ControlBox {
    std::int64_t xMin;
    std::int64_t yMin;
    std::int64_t xMax;
    std::int64_t yMax;
};

std::int64_t Absolute(std::int64_t value) {
    return value < 0 ? -value : value;
}

std::int32_t MostSignificantBit(std::uint32_t value) {
    return value == 0 ? 0 : static_cast<std::int32_t>(std::bit_width(value) - 1);
}

std::int64_t ScaledCoordinate(std::int64_t value, std::int32_t shift) {
    return shift == 0 ? value : value >> shift;
}

ControlBox ControlBoxOf(const GuestOutline& outline) {
    const GuestVector& first = outline.points[0];
    ControlBox box{first.x, first.y, first.x, first.y};
    for (std::uint32_t index = 1; index < outline.n_points; ++index) {
        const GuestVector& point = outline.points[index];
        if (point.x < box.xMin) box.xMin = point.x;
        if (point.x > box.xMax) box.xMax = point.x;
        if (point.y < box.yMin) box.yMin = point.y;
        if (point.y > box.yMax) box.yMax = point.y;
    }
    return box;
}

std::int32_t OrientationOf(const GuestOutline* outline) {
    if (!outline || outline->n_points == 0) return static_cast<std::int32_t>(FT_ORIENTATION_TRUETYPE);
    if (!outline->points) throw std::runtime_error("FT_Outline_Get_Orientation: an outline without a points array is not implemented");

    const ControlBox box = ControlBoxOf(*outline);
    if (box.xMin == box.xMax || box.yMin == box.yMax) return static_cast<std::int32_t>(FT_ORIENTATION_NONE);
    if (box.xMin < -kCoordinateLimit || box.yMin < -kCoordinateLimit || box.xMax > kCoordinateLimit || box.yMax > kCoordinateLimit) {
        return static_cast<std::int32_t>(FT_ORIENTATION_NONE);
    }
    if (outline->n_contours > 0 && !outline->contours) throw std::runtime_error("FT_Outline_Get_Orientation: an outline without a contours array is not implemented");

    const auto xMagnitude = static_cast<std::uint32_t>(Absolute(box.xMax) | Absolute(box.xMin));
    const std::int32_t xShift = std::max(MostSignificantBit(xMagnitude) - kShiftBase, 0);
    const std::int32_t yShift = std::max(MostSignificantBit(static_cast<std::uint32_t>(box.yMax - box.yMin)) - kShiftBase, 0);

    std::uint64_t area = 0;
    std::int32_t last = -1;
    for (std::uint32_t contour = 0; contour < outline->n_contours; ++contour) {
        const std::int32_t end = static_cast<std::int32_t>(outline->contours[contour]);
        if (end >= static_cast<std::int32_t>(outline->n_points)) {
            throw std::runtime_error("FT_Outline_Get_Orientation: a contour that ends past the last point is not implemented");
        }
        const std::int32_t first = last + 1;
        last = end;
        std::int64_t previousX = ScaledCoordinate(outline->points[last].x, xShift);
        std::int64_t previousY = ScaledCoordinate(outline->points[last].y, yShift);
        for (std::int32_t index = first; index <= last; ++index) {
            const std::int64_t currentX = ScaledCoordinate(outline->points[index].x, xShift);
            const std::int64_t currentY = ScaledCoordinate(outline->points[index].y, yShift);
            area += static_cast<std::uint64_t>(currentY - previousY) * static_cast<std::uint64_t>(currentX + previousX);
            previousX = currentX;
            previousY = currentY;
        }
    }

    const std::int64_t signedArea = std::bit_cast<std::int64_t>(area);
    if (signedArea > 0) return static_cast<std::int32_t>(FT_ORIENTATION_POSTSCRIPT);
    if (signedArea < 0) return static_cast<std::int32_t>(FT_ORIENTATION_TRUETYPE);
    return static_cast<std::int32_t>(FT_ORIENTATION_NONE);
}

}

extern "C" {

std::int32_t APS5_VABI FT_Outline_Get_Orientation_nid_postfix(GuestOutline* outline) {
    return OrientationOf(outline);
}

std::int32_t APS5_VABI FT_Outline_GetInsideBorder_nid_postfix(GuestOutline* outline) {
    return OrientationOf(outline) == static_cast<std::int32_t>(FT_ORIENTATION_TRUETYPE) ? static_cast<std::int32_t>(FT_STROKER_BORDER_RIGHT)
                                                                                        : static_cast<std::int32_t>(FT_STROKER_BORDER_LEFT);
}

std::int32_t APS5_VABI FT_Outline_GetOutsideBorder_nid_postfix(GuestOutline* outline) {
    return OrientationOf(outline) == static_cast<std::int32_t>(FT_ORIENTATION_TRUETYPE) ? static_cast<std::int32_t>(FT_STROKER_BORDER_LEFT)
                                                                                         : static_cast<std::int32_t>(FT_STROKER_BORDER_RIGHT);
}

}
