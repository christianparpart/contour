// SPDX-License-Identifier: Apache-2.0
#include <vtrasterizer/ClusterFit.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
