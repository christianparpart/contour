/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <ground/overloaded.h>
#include <terminal/Util.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <utility>

namespace terminal {

class Modifier {
  public:
    enum Key : unsigned {
        None = 0,
        Shift = 1,
        Alt = 2,
        Control = 4,
        Meta = 8,
    };

    constexpr Modifier(Key _key) : mask_{static_cast<unsigned>(_key)} {}

    constexpr Modifier() = default;
	constexpr Modifier(Modifier&&) = default;
	constexpr Modifier(Modifier const&) = default;
	constexpr Modifier& operator=(Modifier&&) = default;
	constexpr Modifier& operator=(Modifier const&) = default;

    constexpr unsigned value() const noexcept { return mask_; }

    constexpr bool none() const noexcept { return value() == 0; }
    constexpr bool some() const noexcept { return value() != 0; }
    constexpr bool shift() const noexcept { return value() & Shift; }
    constexpr bool alt() const noexcept { return value() & Alt; }
    constexpr bool control() const noexcept { return value() & Control; }
    constexpr bool meta() const noexcept { return value() & Meta; }

    constexpr operator unsigned () const noexcept { return mask_; }

    constexpr Modifier& operator|=(Modifier const& _other) noexcept
    {
        mask_ |= _other.mask_;
        return *this;
    }

  private:
    unsigned mask_ = 0;
};

constexpr bool operator<(Modifier _lhs, Modifier _rhs) noexcept
{
    return _lhs.value() < _rhs.value();
}

constexpr bool operator==(Modifier _lhs, Modifier _rhs) noexcept
{
    return _lhs.value() == _rhs.value();
}

std::optional<Modifier::Key> parseModifierKey(std::string const& _key);

constexpr bool operator!(Modifier _modifier) noexcept
{
    return _modifier.none();
}

constexpr bool operator==(Modifier _lhs, Modifier::Key _rhs) noexcept
{
    return static_cast<Modifier::Key>(_lhs.value()) == _rhs;
}

constexpr Modifier operator+(Modifier::Key _lhs, Modifier::Key _rhs) noexcept
{
    return Modifier(static_cast<Modifier::Key>(static_cast<unsigned>(_lhs) | static_cast<unsigned>(_rhs)));
}

/// @returns CSI parameter for given function key modifier
constexpr size_t makeVirtualTerminalParam(Modifier _modifier) noexcept
{
    return 1 + _modifier.value();
}

std::string to_string(Modifier _modifier);

enum class Key {
    // C0 keys
    Enter,
    Backspace,
    Tab,
    Escape,

    // function keys
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    // cursor keys
    DownArrow,
    LeftArrow,
    RightArrow,
    UpArrow,

    // 6-key editing pad
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,

    // numpad keys
    Numpad_NumLock,
    Numpad_Divide,
    Numpad_Multiply,
    Numpad_Subtract,
    Numpad_CapsLock,
    Numpad_Add,
    Numpad_Decimal,
    Numpad_Enter,
    Numpad_Equal,
    Numpad_0,
    Numpad_1,
    Numpad_2,
    Numpad_3,
    Numpad_4,
    Numpad_5,
    Numpad_6,
    Numpad_7,
    Numpad_8,
    Numpad_9,
};

std::optional<Key> parseKey(std::string const& _name);
std::optional<std::variant<Key, char32_t>> parseKeyOrChar(std::string const& _name);

std::string to_string(Key _key);

enum class KeyMode {
    Normal,
    Application
};

struct KeyInputEvent {
	Key key{};
	Modifier modifier{};
};

struct CharInputEvent {
    char32_t value{};
    Modifier modifier{};
};

struct MouseMoveEvent {
    int row;
    int column;
};

enum class MouseButton {
    Left,
    Right,
    Middle,
    WheelUp,
    WheelDown,
};

struct MousePressEvent {
    MouseButton button;
    Modifier modifier{};
};

using MouseEvent = std::variant<MouseButton, MouseMoveEvent>;

using InputEvent = std::variant<
    KeyInputEvent,
    CharInputEvent,
    MousePressEvent
>;

constexpr Modifier modifier(InputEvent _event) noexcept
{
    return std::visit(overloaded{
        [](KeyInputEvent _keyInput) { return _keyInput.modifier; },
        [](CharInputEvent _charInput) { return _charInput.modifier; },
        [](MousePressEvent _mouseInput) { return _mouseInput.modifier; },
    }, _event);
}

constexpr bool operator<(InputEvent const& _lhs, InputEvent const& _rhs) noexcept
{
    if (modifier(_lhs) < modifier(_rhs))
        return true;

    if (modifier(_lhs) == modifier(_rhs))
    {
        if (std::holds_alternative<KeyInputEvent>(_lhs) && std::holds_alternative<KeyInputEvent>(_rhs))
            return std::get<KeyInputEvent>(_lhs).key < std::get<KeyInputEvent>(_rhs).key;
        if (std::holds_alternative<CharInputEvent>(_lhs) && std::holds_alternative<CharInputEvent>(_rhs))
            return std::get<CharInputEvent>(_lhs).value < std::get<CharInputEvent>(_rhs).value;
        if (std::holds_alternative<MousePressEvent>(_lhs) && std::holds_alternative<MousePressEvent>(_rhs))
            return std::get<MousePressEvent>(_lhs).button < std::get<MousePressEvent>(_rhs).button;
        return _lhs.index() < _rhs.index();
    }

    return false;
}

constexpr bool operator==(InputEvent const& _lhs, InputEvent const& _rhs) noexcept
{
    if (modifier(_lhs) == modifier(_rhs))
    {
        if (std::holds_alternative<KeyInputEvent>(_lhs) && std::holds_alternative<KeyInputEvent>(_rhs))
            return std::get<KeyInputEvent>(_lhs).key == std::get<KeyInputEvent>(_rhs).key;
        if (std::holds_alternative<CharInputEvent>(_lhs) && std::holds_alternative<CharInputEvent>(_rhs))
            return std::get<CharInputEvent>(_lhs).value == std::get<CharInputEvent>(_rhs).value;
        if (std::holds_alternative<MousePressEvent>(_lhs) && std::holds_alternative<MousePressEvent>(_rhs))
            return std::get<MousePressEvent>(_lhs).button == std::get<MousePressEvent>(_rhs).button;
    }
    return false;
}

class InputGenerator {
  public:
    using Sequence = std::vector<char>;

    /// Changes the input mode for cursor keys.
    void setCursorKeysMode(KeyMode _mode);

    /// Changes the input mode for numpad keys.
    void setNumpadKeysMode(KeyMode _mode);

    void setApplicationKeypadMode(bool _enable);

    bool normalCursorKeys() const noexcept { return cursorKeysMode_ == KeyMode::Normal; }
    bool applicationCursorKeys() const noexcept { return !normalCursorKeys(); }

    bool numericKeypad() const noexcept { return numpadKeysMode_ == KeyMode::Normal; }
    bool applicationKeypad() const noexcept { return !numericKeypad(); }

    /// Generates input sequences for given input event.
    bool generate(InputEvent const& _inputEvent);

    /// Generates input sequence for a pressed character.
    bool generate(char32_t _characterEvent, Modifier _modifier);

    /// Generates input sequence for a pressed special key.
    bool generate(Key _key, Modifier _modifier);

    /// Generates input sequence for a mouse button press event.
    //TODO: void generate(MouseButton _mouseButton, Modifier _modifier);

    /// Generates input sequence for a mouse move event.
    //TODO: void generate(MouseMoveEvent _mouseMove);

    /// Swaps out the generated input control sequences.
    void swap(Sequence& _other);

  private:
    inline bool emit(std::string _sequence);
    inline bool emit(std::string_view _sequence);
    inline bool emit(char _asciiChar);
    template <typename T, size_t N> inline bool emit(T (&_sequence)[N]);

  private:
    KeyMode cursorKeysMode_ = KeyMode::Normal;
    KeyMode numpadKeysMode_ = KeyMode::Normal;
    Sequence pendingSequence_{};
};

}  // namespace terminal

namespace std {
	template<>
	struct hash<terminal::KeyInputEvent> {
		constexpr size_t operator()(terminal::KeyInputEvent const& _input) const noexcept {
			return (1 << 16) | _input.modifier << 8 | (static_cast<unsigned>(_input.key) & 0xFF);
		}
	};

	template<>
	struct hash<terminal::CharInputEvent> {
		constexpr size_t operator()(terminal::CharInputEvent const& _input) const noexcept {
			return (2 << 16) | _input.modifier << 8 | (static_cast<unsigned>(_input.value) & 0xFF);
		}
	};

	template<>
	struct hash<terminal::MousePressEvent> {
		constexpr size_t operator()(terminal::MousePressEvent const& _input) const noexcept {
			return (3 << 16) | _input.modifier << 8 | (static_cast<unsigned>(_input.button) & 0xFF);
		}
	};

	template<>
	struct hash<terminal::InputEvent> {
		constexpr size_t operator()(terminal::InputEvent const& _input) const noexcept {
            return visit(overloaded{
                [](terminal::KeyInputEvent ev) { return hash<terminal::KeyInputEvent>{}(ev); },
                [](terminal::CharInputEvent ev) { return hash<terminal::CharInputEvent>{}(ev); },
                [](terminal::MousePressEvent ev) { return hash<terminal::MousePressEvent>{}(ev); },
            }, _input);
		}
	};
}
