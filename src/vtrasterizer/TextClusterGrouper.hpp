// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vtbackend/core/Color.hpp>
#include <vtbackend/core/Primitives.hpp>
#include <vtbackend/render/RenderBuffer.hpp>

#include <vtrasterizer/BoxDrawingRenderer.hpp>
#include <vtrasterizer/FontDescriptions.hpp>

#include <gsl/span>
#include <gsl/span_ext>

#include <cstdint>
#include <span>
#include <vector>

namespace vtrasterizer
{

/// This class is responsible for grouping the text to be rendered
/// into clusters of codepoints that share the same text style and color.
class TextClusterGrouper
{
  public:
    struct Events
    {
        virtual ~Events() = default;

        /// Renders one group of cells sharing a style, color, line flags and sizing.
        /// @param codepoints         The group's codepoints, cell after cell.
        /// @param clusters           Per codepoint, the display column its cell starts at, from the group's.
        /// @param cellWidths         Per codepoint, the width of its cell in display columns -- the unit
        ///                           @p clusters count in, so two per grid column on a double-width line.
        /// @param initialPenPosition Where the group's first cell is.
        /// @param style              The group's text style.
        /// @param color              The group's foreground color.
        /// @param flags              The line's flags.
        /// @param sizing             The group's text-sizing scale.
        virtual void renderTextGroup(std::u32string_view codepoints,
                                     gsl::span<unsigned> clusters,
                                     std::span<uint8_t const> cellWidths,
                                     vtbackend::CellLocation initialPenPosition,
                                     TextStyle style,
                                     vtbackend::RGBColor color,
                                     vtbackend::LineFlags flags,
                                     vtbackend::GlyphSizing const& sizing) = 0;

        virtual bool renderBoxDrawingCell(vtbackend::CellLocation position,
                                          char32_t codepoint,
                                          vtbackend::RGBColor foregroundColor,
                                          vtbackend::LineFlags flags) = 0;
    };

    explicit TextClusterGrouper(Events& events);

    /// Must be invoked before a new terminal frame is rendered.
    void beginFrame() noexcept;

    void forceGroupStart() noexcept { _forceUpdateInitialPenPosition = true; }
    void forceGroupEnd() { flushTextClusterGroup(); }

    /// Renders a given terminal's grid cell that has been
    /// transformed into a RenderCell.
    void renderCell(vtbackend::RenderCell const& cell);

    /// Renders one grid cell.
    /// @param position        Where the cell is.
    /// @param graphemeCluster The cell's text.
    /// @param width           How many grid columns the cell spans.
    /// @param foregroundColor The cell's foreground color.
    /// @param style           The cell's text style.
    /// @param flags           The line's flags.
    /// @param sizing          The cell's text-sizing scale.
    void renderCell(vtbackend::CellLocation position,
                    std::u32string_view graphemeCluster,
                    uint8_t width,
                    vtbackend::RGBColor foregroundColor,
                    TextStyle style,
                    vtbackend::LineFlags flags,
                    vtbackend::GlyphSizing const& sizing = {});

    void renderLine(std::u32string_view text,
                    vtbackend::LineOffset lineOffset,
                    vtbackend::RGBColor foregroundColor,
                    TextStyle style,
                    vtbackend::LineFlags flags);

    /// Must be invoked when rendering the terminal's text has finished for this frame.
    void endFrame();

  private:
    /// Puts a sequence of codepoints that belong to the same grid cell at @p position
    /// at the end of the currently filled line.
    void appendCellTextToClusterGroup(vtbackend::CellLocation position,
                                      std::u32string_view codepoints,
                                      uint8_t width,
                                      TextStyle style,
                                      vtbackend::RGBColor color,
                                      vtbackend::LineFlags flags,
                                      vtbackend::GlyphSizing const& sizing);

    void flushTextClusterGroup();

    Events& _events;

    // pen-start position of this text group
    vtbackend::CellLocation _initialPenPosition {};

    // uniform text style for this text group
    TextStyle _style = TextStyle::Invalid;

    // uniform text color for this text group
    vtbackend::RGBColor _color {};
    vtbackend::LineFlags _lineFlags = vtbackend::LineFlag::None;

    // codepoints within this text group with
    // uniform unicode properties (script, language, direction).
    std::vector<char32_t> _codepoints;

    // For each codepoint, the column its cell starts at, counted from the group's first. @see RunPen
    std::vector<unsigned> _clusters;

    // For each codepoint, the width of its cell in display columns.
    std::vector<uint8_t> _cellWidths;

    /// The scale the current group is being drawn at. Part of the group's identity: a cell drawn at
    /// a different size cannot share a shaping run with its neighbours.
    vtbackend::GlyphSizing _sizing {};

    bool _forceUpdateInitialPenPosition = false;
};

} // namespace vtrasterizer
