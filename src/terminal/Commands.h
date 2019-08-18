#pragma once

#include <terminal/Color.h>

#include <functional>
#include <string>
#include <string_view>
#include <variant>

namespace terminal {

using cursor_pos_t = size_t;

/// Screen coordinates between 1..n including.
struct Coordinate {
    cursor_pos_t row;
    cursor_pos_t column;
};

constexpr inline bool operator==(Coordinate const& a, Coordinate const& b) noexcept
{
    return a.row == b.row && a.column == b.column;
}

constexpr inline bool operator!=(Coordinate const& a, Coordinate const& b) noexcept
{
    return !(a == b);
}

enum class GraphicsRendition {
    Reset = 0,              //!< Reset any rendition (style as well as foreground / background coloring).

    Bold = 1,               //!< Bold glyph width
    Faint = 2,              //!< Decreased intensity
    Italic = 3,             //!< Italic glyph
    Underline = 4,          //!< Underlined glyph
    Blinking = 5,           //!< Blinking glyph
    Inverse = 7,            //!< Swaps foreground with background color.
    Hidden = 8,             //!< Glyph hidden (somewhat like space character).
    CrossedOut = 9,         //!< Crossed out glyph space.
    DoublyUnderlined = 21,  //!< Underlined with two lines.

    Normal = 22,            //!< Neither Bold nor Faint.
    NoItalic = 23,          //!< Reverses Italic.
    NoUnderline = 24,       //!< Reverses Underline.
    NoBlinking = 25,        //!< Reverses Blinking.
    NoInverse = 27,         //!< Reverses Inverse.
    NoHidden = 28,          //!< Reverses Hidden (Visible).
    NoCrossedOut = 29,      //!< Reverses CrossedOut.
};

std::string to_string(GraphicsRendition s);

enum class Mode {
    // {{{ normal modes
    KeyboardAction,
    Insert,
    SendReceive,
    AutomaticLinefeed,
    // }}}

    // {{{ DEC modes
    UseApplicationCursorKeys,
    DesignateCharsetUSASCII,
    Columns132,
    SmoothScroll,
    ReverseVideo,
    /**
     * DECOM - Origin Mode.
     *
     * This control function sets the origin for the cursor.
     * DECOM determines if the cursor position is restricted to inside the page margins.
     * When you power up or reset the terminal, you reset origin mode.
     *
     * Default: Origin is at the upper-left of the screen, independent of margins.
     *
     * When DECOM is set, the home cursor position is at the upper-left corner of the screen, within the margins.
     * The starting point for line numbers depends on the current top margin setting.
     * The cursor cannot move outside of the margins.
     *
     * When DECOM is reset, the home cursor position is at the upper-left corner of the screen.
     * The starting point for line numbers is independent of the margins.
     * The cursor can move outside of the margins.
     */
    CursorRestrictedToMargin, 

    /**
     * DECAWM - Autowrap Mode.
     *
     * This control function determines whether or not received characters automatically wrap
     * to the next line when the cursor reaches the right border of a page in page memory.
     *
     * If the DECAWM function is set, then graphic characters received when the cursor
     * is at the right border of the page appear at the beginning of the next line.
     *
     * Any text on the page scrolls up if the cursor is at the end of the scrolling region.
     */
    AutoWrap,

    PrinterExtend,
    LeftRightMargin,

    ShowToolbar,
    BlinkingCursor,
    VisibleCursor,
    ShowScrollbar,
    UseAlternateScreen,
    BracketedPaste,
    // }}}
};

constexpr std::string_view to_code(Mode m)
{
    switch (m)
    {
        case Mode::KeyboardAction: return "2";
        case Mode::Insert: return "4";
        case Mode::SendReceive: return "12";
        case Mode::AutomaticLinefeed: return "20";

        // DEC set-mode
        case Mode::UseApplicationCursorKeys: return "?1";
        case Mode::DesignateCharsetUSASCII: return "?2";
        case Mode::Columns132: return "?3";
        case Mode::SmoothScroll: return "?4";
        case Mode::ReverseVideo: return "?5";
        case Mode::CursorRestrictedToMargin: return "?6"; 
        case Mode::AutoWrap: return "?7";
        case Mode::ShowToolbar: return "?10";
        case Mode::BlinkingCursor: return "?12";
        case Mode::PrinterExtend: return "?19";
        case Mode::VisibleCursor: return "?25";
        case Mode::ShowScrollbar: return "?30";
        case Mode::UseAlternateScreen: return "?47";
        case Mode::LeftRightMargin: return "?69";
        case Mode::BracketedPaste: return "?2004";
    }
    return "0";
}

std::string to_string(Mode m);

enum class CharsetTable {
    G0 = 0,
    G1 = 1,
    G2 = 2,
    G3 = 3
};

std::string to_string(CharsetTable i);

enum class Charset {
    Special, // Special Character and Line Drawing Set

    UK,
    USASCII,
    German,

    // ... TODO
};

std::string to_string(Charset charset);

struct Bell {};

/// LF - Causes a line feed or a new line operation, depending on the setting of line feed/new line mode.
struct Linefeed {};

struct Backspace {};
struct FullReset {};

struct DeviceStatusReport {};
struct ReportCursorPosition {};
struct SendDeviceAttributes {};
struct SendTerminalId {};

struct ClearToEndOfScreen {};
struct ClearToBeginOfScreen {};
struct ClearScreen {};

struct ClearScrollbackBuffer {};

/// SU - Pan Down.
///
/// This control function moves the user window down a specified number of lines in page memory.
struct ScrollUp {
    /// This is the number of lines to move the user window down in page memory.
    /// @p n new lines appear at the bottom of the display.
    /// @p n old lines disappear at the top of the display.
    /// You cannot pan past the bottom margin of the current page.
    cursor_pos_t n;
};

/// SD - Pan Up.
///
/// This control function moves the user window up a specified number of lines in page memory.
struct ScrollDown {
    /// This is the number of lines to move the user window up in page memory.
    /// @p n new lines appear at the top of the display.
    /// @p n old lines disappear at the bottom of the display.
    /// You cannot pan past the top margin of the current page.
    cursor_pos_t n;
};

struct ClearToEndOfLine {};
struct ClearToBeginOfLine {};
struct ClearLine {};

/// IL - Insert Line
///
/// This control function inserts one or more blank lines, starting at the cursor.
///
/// As lines are inserted, lines below the cursor and in the scrolling region move down.
/// Lines scrolled off the page are lost. IL has no effect outside the page margins.
struct InsertLines { cursor_pos_t n; };

/// DL - Delete Line
///
/// This control function deletes one or more lines in the scrolling region,
/// starting with the line that has the cursor.
///
/// As lines are deleted, lines below the cursor and in the scrolling region move up.
/// The terminal adds blank lines with no visual character attributes at the bottom of the scrolling region.
/// If Pn is greater than the number of lines remaining on the page, DL deletes only the remaining lines.
///
/// DL has no effect outside the scrolling margins.
struct DeleteLines {
    /// This is the number of lines to delete.
    cursor_pos_t n;
};

/// DCH - Delete Character.
///
/// This control function deletes one or more characters from the cursor position to the right.
///
/// As characters are deleted, the remaining characters between the cursor and right margin move to the left.
/// Character attributes move with the characters.
/// The terminal adds blank spaces with no visual character attributes at the right margin.
/// DCH has no effect outside the scrolling margins.
struct DeleteCharacters {
    /// This is the number of characters to delete.
    ///
    /// If this value is greater than the number of characters between the cursor and the right margin,
    /// then DCH only deletes the remaining characters.
    cursor_pos_t n;
};

/// CUU - Cursor Up.
/// Moves the cursor up a specified number of lines in the same column.
/// The cursor stops at the top margin.
/// If the cursor is already above the top margin, then the cursor stops at the top line.
struct MoveCursorUp {
    /// This is the number of lines to move the cursor up.
    cursor_pos_t n;
};

/// CUD - Cursor Down.
///
/// This control function moves the cursor down a specified number of lines in the same column.
/// The cursor stops at the bottom margin.
/// If the cursor is already below the bottom margin, then the cursor stops at the bottom line.
struct MoveCursorDown {
    /// This is the number of lines to move the cursor down.
    cursor_pos_t n;
};

/// CUF - Cursor Forward.
///
/// This control function moves the cursor to the right by a specified number of columns.
/// The cursor stops at the right border of the page.
struct MoveCursorForward {
    /// This is the number of columns to move the cursor to the right.
    cursor_pos_t n;
};

/// CUB - Cursor Backward.
///
/// This control function moves the cursor to the left by a specified number of columns.
/// The cursor stops at the left border of the page.
struct MoveCursorBackward {
    /// This is the number of columns to move the cursor to the left.
    cursor_pos_t n;
};

/// CHA - Cursor Horizontal Absolute.
///
/// Move the active position to the n-th character of the active line.
///
/// The active position is moved to the n-th character position of the active line.
struct MoveCursorToColumn {
    /// This is the number of active positions to the n-th character of the active line.
    cursor_pos_t column;
};

/// Moves the cursor to the left margin on the current line.
struct MoveCursorToBeginOfLine {};

/// CUP - Cursor Position.
///
/// This control function moves the cursor to the specified line and column.
/// The starting point for lines and columns depends on the setting of origin mode (DECOM).
/// CUP applies only to the current page.
struct MoveCursorTo {
    /// This is the number of the line to move to. If the value is 0 or 1, then the cursor moves to line 1.
    cursor_pos_t row;

    /// This is the number of the column to move to. If the value is 0 or 1, then the cursor moves to column 1.
    cursor_pos_t column;
};

struct MoveCursorToNextTab {};

struct HideCursor {};
struct ShowCursor {};
struct SaveCursor {};
struct RestoreCursor {};

struct SetForegroundColor { Color color; };
struct SetBackgroundColor { Color color; };
struct SetGraphicsRendition { GraphicsRendition rendition; };

struct AppendChar { wchar_t ch; };

struct SetMode { Mode mode; bool enable; };

/// DECSTBM - Set Top and Bottom Margins
///
/// This control function sets the top and bottom margins for the current page.
/// You cannot perform scrolling outside the margins.
///
/// Default: Margins are at the page limits.
///
/// The value of the top margin (Pt) must be less than the bottom margin (Pb).
/// The maximum size of the scrolling region is the page size.
///
/// DECSTBM moves the cursor to column 1, line 1 of the page.
struct SetTopBottomMargin {
    /// The line number for the top margin.
    /// Default: 1
    size_t top;

    /// The line number for the bottom margin.
    /// Default: current number of lines per screen
    size_t bottom;
};

struct SetLeftRightMargin { size_t left; size_t right; };

enum class MouseProtocol {
    X10 = 9,
    VT200 = 1000,
    VT200_Highlight = 1001,
    ButtonEvent = 1002,
    AnyEvent = 1003,
    FocusEvent = 1004,
    Extended = 1005,
    SGR = 1006,
    URXVT = 1015,

    AlternateScroll = 1007,
};

std::string to_string(MouseProtocol protocol);
unsigned to_code(MouseProtocol protocol);

struct SendMouseEvents { MouseProtocol protocol; bool enable; };

struct AlternateKeypadMode { bool enable; };

struct DesignateCharset { CharsetTable table; Charset charset; };

//! Selects given CharsetTable for the very next character only.
struct SingleShiftSelect { CharsetTable table; };

/// IND - Index
///
/// Moves the cursor down one line in the same column.
/// If the cursor is at the bottom margin, then the screen performs a scroll-up.
struct Index {};

/// RI - Reverse Index
///
/// Moves the cursor up, but also scrolling the screen if already at top
struct ReverseIndex {};

/// DECBI - Back Index.
///
/// This control function moves the cursor backward one column.
/// If the cursor is at the left margin, then all screen data within the margin moves one column to the right.
/// The column that shifted past the right margin is lost.
///
/// DECBI adds a new column at the left margin with no visual attributes.
/// DECBI is not affected by the margins.
/// If the cursor is at the left border of the page when the terminal receives DECBI, then the terminal ignores DECBI.
struct BackIndex {};

/// DECFI - Forward Index
///
/// This control function moves the cursor forward one column.
/// If the cursor is at the right margin, then all screen data within the margins moves one column to the left.
/// The column shifted past the left margin is lost.
///
/// DECFI adds a new column at the right margin, with no visual attributes.
/// DECFI is not affected by the margins.
/// If the cursor is at the right border of the page when the terminal receives DECFI,
/// then the terminal ignores DECFI.
struct ForwardIndex {};

/// DECALN - Screen Alignment Pattern.
///
/// This control function fills the complete screen area with a test pattern used for adjusting screen alignment.
/// Normally, only manufacturing and service personnel would use DECALN.
///
/// DECALN sets the margins to the extremes of the page, and moves the cursor to the home position.
struct ScreenAlignmentPattern {};

// OSC commands:
struct ChangeWindowTitle { std::string title; };
struct ChangeIconName { std::string name; };

using Command = std::variant<
    Bell,
    Linefeed,
    Backspace,
    FullReset,

    DeviceStatusReport,
    ReportCursorPosition,
    SendDeviceAttributes,
    SendTerminalId,

    ClearToEndOfScreen,
    ClearToBeginOfScreen,
    ClearScreen,

    ClearScrollbackBuffer,
    ScrollUp,
    ScrollDown,

    ClearToEndOfLine,
    ClearToBeginOfLine,
    ClearLine,

    InsertLines,
    DeleteLines,
    DeleteCharacters,

    MoveCursorUp,
    MoveCursorDown,
    MoveCursorForward,
    MoveCursorBackward,
    MoveCursorToColumn,
    MoveCursorToBeginOfLine,
    MoveCursorTo,
    MoveCursorToNextTab,
    HideCursor,
    ShowCursor,
    SaveCursor,
    RestoreCursor,

    Index,
    ReverseIndex,
    BackIndex,
    ForwardIndex,

    SetForegroundColor,
    SetBackgroundColor,
    SetGraphicsRendition,

    SetMode,

    SendMouseEvents,

    AlternateKeypadMode,
    DesignateCharset,
    SingleShiftSelect,
    SetTopBottomMargin,
    SetLeftRightMargin,
    ScreenAlignmentPattern,

    // OSC
    ChangeWindowTitle,
    ChangeIconName,

    // Ground
    AppendChar
>;

std::string to_string(Command const& cmd);

}  // namespace terminal