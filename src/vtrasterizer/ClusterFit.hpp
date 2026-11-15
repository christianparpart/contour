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

/// Where one glyph of a colour cluster lands, horizontally, before the cluster is centred.
struct PlacedTile
{
    int x;     ///< Left edge, in pixels: pen position, plus the font's bearing and offset.
    int width; ///< Drawn width, in pixels.
};

/// How far to move a colour cluster so that it is centred in its cells as a whole.
///
/// An emoji is centred in its cells, because emoji fonts place their glyphs inconsistently. A font may
/// compose one emoji from several glyphs, though -- Segoe UI Emoji draws a family as narrower people it
/// overlaps with its own offsets -- and centring each of those on its own stacks them all in the middle,
/// and the advance between them then pushes the last ones out to the right. So the cluster's glyphs keep
/// the places the font gave them, and the whole of them is centred instead: for a single glyph, the same.
///
/// @param tiles    Each inked glyph of the cluster, where the font put it.
/// @param boxLeft  The left edge of the cluster's first cell, in the same coordinates as @p tiles.
/// @param boxWidth The cluster's width in pixels.
/// @return The shift, in pixels, to add to every glyph of the cluster.
[[nodiscard]] constexpr int centringShift(std::span<PlacedTile const> tiles,
                                          int boxLeft,
                                          int boxWidth) noexcept
{
    if (tiles.empty())
        return 0;
    auto left = tiles.front().x;
    auto right = tiles.front().x + tiles.front().width;
    for (auto const& tile: tiles)
    {
        left = std::min(left, tile.x);
        right = std::max(right, tile.x + tile.width);
    }
    return boxLeft + ((boxWidth - (right - left)) / 2) - left;
}

} // namespace vtrasterizer
