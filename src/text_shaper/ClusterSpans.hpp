// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace text
{

/// A shaped glyph reduced to what font fallback needs in order to decide about it.
struct ShapedGlyphRef
{
    unsigned cluster {}; ///< The input cluster the shaper assigned to this glyph.
    bool missing {};     ///< Whether the shaping font had no glyph for it (glyph index 0, .notdef).
    int advance {};      ///< The glyph's horizontal advance, in pixels. Zero for a hidden joiner or a mark.
};

/// Whether a cluster group is rendered by the font that shaped it, and if not, why not.
enum class GroupResolution : uint8_t
{
    Resolved,   ///< The font renders the group.
    NotDef,     ///< The font has no glyph for at least one of its codepoints.
    Fragmented, ///< The font has every glyph, but draws one emoji sequence as several -- see
                ///< FragmentationRule.
};

/// Whether a cluster shaped into more than one advancing glyph counts as rendered.
///
/// In text it does: a base and its combining marks, or a conjunct, legitimately shape to several glyphs.
/// An emoji sequence does not: a font that knows "people with bunny ears" in two skin tones draws it as one
/// glyph, and one that does not draws its pieces side by side -- a person, a rabbit and a person -- which no
/// reader takes for the sequence. Font fallback must look further for such a cluster, exactly as for a
/// missing glyph.
enum class FragmentationRule : uint8_t
{
    Ignore,             ///< Text: several glyphs per cluster are fine.
    OneGlyphPerCluster, ///< Emoji: a cluster with more than one advancing glyph is Fragmented.
};

/// A maximal group of consecutive shaped glyphs sharing one cluster value, together with the half-open
/// range of input codepoints that produced them.
struct ClusterGroup
{
    std::size_t glyphBegin {};     ///< First glyph index, into the shaped run.
    std::size_t glyphEnd {};       ///< One past the last glyph index.
    std::size_t codepointBegin {}; ///< First input codepoint index, into the run's codepoints.
    std::size_t codepointEnd {};   ///< One past the last input codepoint index.
    GroupResolution resolution {}; ///< Whether the shaping font renders this group.

    /// @return Whether this group maps to no input codepoints at all.
    [[nodiscard]] constexpr bool empty() const noexcept { return codepointBegin >= codepointEnd; }

    /// @return Whether this group must be offered to a fallback font.
    [[nodiscard]] constexpr bool unresolved() const noexcept
    {
        return resolution != GroupResolution::Resolved;
    }

    [[nodiscard]] constexpr bool operator==(ClusterGroup const&) const noexcept = default;
};

/// Segments a shaped run into cluster groups, mapping each group back to the input codepoints that
/// produced it.
///
/// This is the cluster-alignment step of font fallback. A glyph the shaping font could not render must
/// never be re-shaped in isolation: the shaper may have merged several input clusters into a single
/// glyph (a ligature), or emitted several glyphs for a single cluster (a base plus its combining marks).
/// Only a whole cluster group may be handed to another font.
///
/// Relies on the shaper's monotone-cluster guarantee: output glyph clusters are non-decreasing, glyphs
/// of one cluster are contiguous, and a merged cluster carries the smallest of the input cluster values
/// it was built from. A group's codepoint range is therefore
/// [first input index with cluster >= c(k), first input index with cluster >= c(k+1)), which covers a
/// merged ligature in full and never splits a cluster.
///
/// @param glyphs        The shaped glyphs, in visual (left-to-right) order.
/// @param inputClusters The per-codepoint cluster values handed to the shaper. These are cell indices,
///                      not codepoint offsets: they need not start at zero, and all codepoints of one
///                      cell share a value. Must be non-decreasing.
/// @return The cluster groups covering the whole run, or std::nullopt if either input violates the
///         monotone-cluster guarantee, in which case the caller must treat the run as indivisible.
[[nodiscard]] inline std::optional<std::vector<ClusterGroup>> clusterGroups(
    std::span<ShapedGlyphRef const> glyphs, std::span<unsigned const> inputClusters)
{
    if (!std::ranges::is_sorted(inputClusters))
        return std::nullopt;

    auto groups = std::vector<ClusterGroup> {};
    if (glyphs.empty())
        return groups;

    groups.reserve(glyphs.size());

    // Both sequences are non-decreasing, so one forward walk over the codepoints serves every group: the
    // codepoints a group owns end exactly where the next group's cluster begins.
    auto codepointCursor = std::size_t { 0 };
    auto const advanceTo = [&codepointCursor, inputClusters](unsigned cluster) noexcept {
        while (codepointCursor < inputClusters.size() && inputClusters[codepointCursor] < cluster)
            ++codepointCursor;
        return codepointCursor;
    };

    for (auto glyphIndex = std::size_t { 0 }; glyphIndex < glyphs.size();)
    {
        auto const cluster = glyphs[glyphIndex].cluster;

        // Groups are maximal runs of equal cluster, so each new group's cluster must strictly exceed the
        // previous one. Anything else means the run is not monotone and must not be split.
        if (!groups.empty() && cluster <= glyphs[groups.back().glyphBegin].cluster)
            return std::nullopt;

        auto glyphEnd = glyphIndex;
        auto missing = false;
        while (glyphEnd < glyphs.size() && glyphs[glyphEnd].cluster == cluster)
        {
            missing = missing || glyphs[glyphEnd].missing;
            ++glyphEnd;
        }

        auto const codepointBegin = advanceTo(cluster);
        auto const codepointEnd =
            glyphEnd < glyphs.size() ? advanceTo(glyphs[glyphEnd].cluster) : inputClusters.size();

        groups.emplace_back(
            ClusterGroup { .glyphBegin = glyphIndex,
                           .glyphEnd = glyphEnd,
                           .codepointBegin = codepointBegin,
                           .codepointEnd = codepointEnd,
                           .resolution = missing ? GroupResolution::NotDef : GroupResolution::Resolved });
        glyphIndex = glyphEnd;
    }

    // The run's codepoints are owned end to end, even where the shaper dropped a leading or trailing
    // codepoint outright (a default-ignorable, say) and no glyph claims it.
    groups.front().codepointBegin = 0;
    groups.back().codepointEnd = inputClusters.size();

    return groups;
}

/// @return The whole run as a single, indivisible group.
///
/// The conservative answer for when clusterGroups() has refused to segment a run: with no trustworthy
/// mapping from glyphs back to codepoints, the run may not be split, and a fallback font must be offered
/// all of it at once -- which is what font fallback did for every run before it learned to split them.
///
/// @param glyphs         The shaped glyphs of the run.
/// @param codepointCount The number of input codepoints the run was shaped from.
[[nodiscard]] inline ClusterGroup indivisibleGroup(std::span<ShapedGlyphRef const> glyphs,
                                                   std::size_t codepointCount)
{
    return ClusterGroup { .glyphBegin = 0,
                          .glyphEnd = glyphs.size(),
                          .codepointBegin = 0,
                          .codepointEnd = codepointCount,
                          .resolution = std::ranges::any_of(
                                            glyphs, [](ShapedGlyphRef const& ref) { return ref.missing; })
                                            ? GroupResolution::NotDef
                                            : GroupResolution::Resolved };
}

/// Marks each group drawn as the pieces of an emoji sequence as Fragmented.
///
/// A group is pieces when more than one of its glyphs advances AND together they advance further than one
/// emoji could: @p maxWholeAdvance. Both halves matter. Zero-advance glyphs do not count, for the joiners of
/// an uncombined sequence are hidden at zero advance between its pieces. And several advancing glyphs alone
/// are not pieces: Segoe UI Emoji draws a family as narrower people the font overlaps with its own offsets,
/// together about one emoji wide, where uncombined pieces advance one whole emoji EACH.
/// A group already NotDef stays NotDef -- a missing glyph is the stronger statement, and both send the group
/// down the same fallback chain.
///
/// @param groups          The cluster groups of one shaped run.
/// @param glyphs          The shaped glyphs the groups index into.
/// @param maxWholeAdvance The widest, in pixels, a cluster may advance and still be one emoji.
inline void markFragmentedGroups(std::span<ClusterGroup> groups,
                                 std::span<ShapedGlyphRef const> glyphs,
                                 int maxWholeAdvance) noexcept
{
    for (auto& group: groups)
    {
        if (group.resolution != GroupResolution::Resolved)
            continue;
        auto const members = glyphs.subspan(group.glyphBegin, group.glyphEnd - group.glyphBegin);
        auto advancing = 0;
        auto totalAdvance = 0;
        for (auto const& ref: members)
        {
            advancing += ref.advance != 0 ? 1 : 0;
            totalAdvance += ref.advance;
        }
        if (advancing > 1 && totalAdvance > maxWholeAdvance)
            group.resolution = GroupResolution::Fragmented;
    }
}

/// Coalesces neighbouring uncovered groups into single fallback segments.
///
/// Adjacent codepoints that the shaping font is missing belong to one fallback lookup, not several: the
/// prompt ornament and the non-breaking space that follows it must be offered to a fallback font
/// together, so that one font can answer for both. Covered groups pass through untouched, and the
/// returned segments still cover the run end to end, in order.
///
/// @param groups The cluster groups of one shaped run, as returned by clusterGroups().
/// @return The same run, with runs of missing groups merged.
[[nodiscard]] inline std::vector<ClusterGroup> mergeAdjacentMissing(std::span<ClusterGroup const> groups)
{
    auto segments = std::vector<ClusterGroup> {};
    segments.reserve(groups.size());

    for (auto index = std::size_t { 0 }; index < groups.size();)
    {
        if (!groups[index].unresolved())
        {
            segments.emplace_back(groups[index]);
            ++index;
            continue;
        }

        auto last = index;
        auto anyNotDef = groups[index].resolution == GroupResolution::NotDef;
        while (last + 1 < groups.size() && groups[last + 1].unresolved())
        {
            ++last;
            anyNotDef = anyNotDef || groups[last].resolution == GroupResolution::NotDef;
        }

        segments.emplace_back(
            ClusterGroup { .glyphBegin = groups[index].glyphBegin,
                           .glyphEnd = groups[last].glyphEnd,
                           .codepointBegin = groups[index].codepointBegin,
                           .codepointEnd = groups[last].codepointEnd,
                           .resolution = anyNotDef ? GroupResolution::NotDef : GroupResolution::Fragmented });
        index = last + 1;
    }

    return segments;
}

/// Divides a shaped run into the segments font fallback works on.
///
/// Covered segments are kept as they are; each maximal stretch the shaping font could not render becomes
/// one segment, to be offered to a fallback font whole. A run whose clusters cannot be trusted to be
/// monotone yields a single indivisible segment -- the conservative answer, and what every run used to
/// get before fallback learned to split them.
///
/// @param glyphs   The shaped glyphs of the run, in visual (left-to-right) order.
/// @param clusters The per-codepoint cluster values the run was shaped with.
/// @param rule     Whether a cluster shaped into several advancing glyphs counts as unresolved.
/// @param maxWholeAdvance Under FragmentationRule::OneGlyphPerCluster, the widest, in pixels, a cluster may
///                        advance and still be one emoji (@see markFragmentedGroups). Ignored otherwise.
/// @return The segments, in order, covering the run end to end.
[[nodiscard]] inline std::vector<ClusterGroup> fallbackSegments(std::span<ShapedGlyphRef const> glyphs,
                                                                std::span<unsigned const> clusters,
                                                                FragmentationRule rule,
                                                                int maxWholeAdvance)
{
    auto groups = clusterGroups(glyphs, clusters)
                      .value_or(std::vector<ClusterGroup> { indivisibleGroup(glyphs, clusters.size()) });
    if (rule == FragmentationRule::Ignore)
        return mergeAdjacentMissing(groups);

    // In an emoji run every cluster is its own question for fallback. Merged with a neighbour, a sequence
    // could only be given a font that renders the neighbour too -- so one no font knows would deny it the
    // font that combines it, or turn its pieces into replacement glyphs.
    markFragmentedGroups(groups, glyphs, maxWholeAdvance);
    return groups;
}

} // namespace text
