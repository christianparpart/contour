// SPDX-License-Identifier: Apache-2.0
#include <vtrasterizer/ClusterFit.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <vector>

using namespace vtrasterizer;
using vtbackend::Height;
using vtbackend::ImageSize;
using vtbackend::Width;

namespace
{

[[nodiscard]] std::vector<ImageSize> piecesOf(int count, int width, int height)
{
    return std::vector<ImageSize>(static_cast<size_t>(count),
                                  ImageSize { Width::cast_from(width), Height::cast_from(height) });
}

[[nodiscard]] std::vector<int> xs(std::vector<FittedPiece> const& fitted)
{
    return std::ranges::to<std::vector<int>>(std::views::transform(fitted, &FittedPiece::x));
}

/// Every piece lies inside a box @p boxWidth wide.
[[nodiscard]] bool insideBox(std::vector<FittedPiece> const& fitted, int boxWidth)
{
    return std::ranges::all_of(fitted, [&](FittedPiece const& piece) {
        return piece.x >= 0 && piece.width >= 1 && piece.x + piece.width <= boxWidth;
    });
}

} // namespace

TEST_CASE("ClusterFit.fits_three_pieces_into_two_cells", "[ClusterFit]")
{
    // A cell 10x20: each piece was rasterized to the two-cell emoji box, 20x20, so three of them are 60
    // wide and must shrink to a third.
    auto const pieces = piecesOf(3, 20, 20);
    auto const fitted = fitClusterPieces(pieces, 20, 20);
    REQUIRE(fitted.size() == 3);
    for (auto const& piece: fitted)
    {
        CHECK(piece.width == 6);
        CHECK(piece.height == 6);
    }
    CHECK(xs(fitted) == std::vector { 1, 7, 13 }); // a row 18 wide, centred in 20
    CHECK(insideBox(fitted, 20));
}

TEST_CASE("ClusterFit.fits_into_one_cell", "[ClusterFit]")
{
    auto const pieces = piecesOf(2, 20, 20);
    auto const fitted = fitClusterPieces(pieces, 10, 20);
    REQUIRE(fitted.size() == 2);
    CHECK(fitted[0].width == 5);
    CHECK(fitted[1].width == 5);
    CHECK(xs(fitted) == std::vector { 0, 5 });
    CHECK(insideBox(fitted, 10));
}

TEST_CASE("ClusterFit.a_piece_that_already_fits_is_not_enlarged", "[ClusterFit]")
{
    auto const pieces = piecesOf(2, 4, 4);
    auto const fitted = fitClusterPieces(pieces, 20, 20);
    REQUIRE(fitted.size() == 2);
    CHECK(fitted[0].width == 4);
    CHECK(fitted[0].height == 4);
    CHECK(xs(fitted) == std::vector { 6, 10 });
}

TEST_CASE("ClusterFit.double_width_line", "[ClusterFit]")
{
    // On a DECDWL line the cluster is four display columns wide; the pieces were rasterized for that box.
    auto const pieces = piecesOf(3, 40, 20);
    auto const fitted = fitClusterPieces(pieces, 40, 20);
    REQUIRE(fitted.size() == 3);
    CHECK(insideBox(fitted, 40));
}

TEST_CASE("ClusterFit.a_tall_piece_is_bounded_by_the_cell_height", "[ClusterFit]")
{
    auto const pieces = piecesOf(1, 10, 40);
    auto const fitted = fitClusterPieces(pieces, 20, 20);
    REQUIRE(fitted.size() == 1);
    CHECK(fitted[0].height == 20);
    CHECK(fitted[0].width == 5);
}

TEST_CASE("ClusterFit.no_pieces", "[ClusterFit]")
{
    CHECK(fitClusterPieces({}, 20, 20).empty());
}

TEST_CASE("ClusterFit.centringShift", "[ClusterFit]")
{
    SECTION("one glyph is centred in its box, wherever its bearing put it")
    {
        // A 12 px wide emoji whose bearing placed it at x=3 in a 20 px box starting at 0.
        auto const tiles = std::array { PlacedTile { .x = 3, .width = 12 } };
        CHECK(centringShift(tiles, 0, 20) == 1); // drawn at 4: (20 - 12) / 2
    }

    SECTION("a composed cluster is centred as a whole, its glyphs keeping their places")
    {
        // Segoe UI Emoji's family: two narrower people the font overlaps with its own offsets.
        auto const tiles =
            std::array { PlacedTile { .x = 2, .width = 9 }, PlacedTile { .x = 8, .width = 9 } };
        // The union is [2, 17), 15 wide: centred in [10, 30) it starts at 12.5 -> 12, a shift of 10.
        CHECK(centringShift(tiles, 10, 20) == 10);
    }

    SECTION("nothing to centre")
    {
        CHECK(centringShift({}, 10, 20) == 0);
    }
}
