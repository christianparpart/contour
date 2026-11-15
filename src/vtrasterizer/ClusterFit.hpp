// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vtbackend/core/Primitives.hpp>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <span>
#include <vector>

namespace vtrasterizer
{

/// Where one piece of a fragmented cluster is drawn, and how large.
struct FittedPiece
{
    int x;      ///< Left edge, in pixels from the cluster's left edge.
    int width;  ///< Drawn width, in pixels.
    int height; ///< Drawn height, in pixels.
};

/// Lays the pieces of a fragmented cluster side by side inside the cluster's own cells.
///
/// An emoji sequence that no font combines arrives as its pieces -- "people with bunny ears" in two skin
/// tones as a person, a rabbit and a person -- each rasterized to fill the cluster's whole box. Drawn where
/// the font's advances put them, they ran many cells to the right, over unrelated text (#2381). Instead they
/// share the box: scaled down together by one factor, so that the row of them fits its width and the tallest
/// fits the cell, never enlarged, and the row centred horizontally.
///
/// @param pieces    The drawn size of each piece, at the size it was rasterized to.
/// @param boxWidth  The cluster's width in pixels: its width in display columns times the cell width.
/// @param boxHeight The cell height in pixels.
/// @return One entry per piece, in order.
[[nodiscard]] inline std::vector<FittedPiece> fitClusterPieces(std::span<vtbackend::ImageSize const> pieces,
                                                               int boxWidth,
                                                               int boxHeight)
{
    if (pieces.empty())
        return {};

    auto const widthOf = [](vtbackend::ImageSize size) {
        return unbox<int>(size.width);
    };
    auto const heightOf = [](vtbackend::ImageSize size) {
        return unbox<int>(size.height);
    };

    auto totalWidth = 0;
    for (auto const piece: pieces)
        totalWidth += widthOf(piece);
    auto const tallest = std::ranges::max(pieces | std::views::transform(heightOf));

    auto factor = 1.0;
    if (totalWidth > 0)
        factor = std::min(factor, static_cast<double>(boxWidth) / totalWidth);
    if (tallest > 0)
        factor = std::min(factor, static_cast<double>(boxHeight) / tallest);

    auto const scaled = [factor](int value) {
        return std::max(1, static_cast<int>(std::floor(static_cast<double>(value) * factor)));
    };

    auto fitted = std::vector<FittedPiece> {};
    fitted.reserve(pieces.size());
    auto rowWidth = 0;
    for (auto const piece: pieces)
    {
        auto const width = scaled(widthOf(piece));
        fitted.emplace_back(FittedPiece { .x = rowWidth, .width = width, .height = scaled(heightOf(piece)) });
        rowWidth += width;
    }

    auto const indent = std::max(0, (boxWidth - rowWidth) / 2);
    for (auto& piece: fitted)
        piece.x += indent;
    return fitted;
}

} // namespace vtrasterizer
