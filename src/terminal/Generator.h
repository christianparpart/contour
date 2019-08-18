#pragma once

#include <terminal/Commands.h>
#include <terminal/UTF8.h>

#include <fmt/format.h>

#include <functional>
#include <iostream>
#include <string_view>
#include <vector>

namespace terminal {

/// Encodes Command stream into ANSI codes and text.
class Generator {
  public:
    using Writer = std::function<void(char const*, size_t)>;

    explicit Generator(Writer writer) : writer_{writer} {}
    explicit Generator(std::ostream& output) : Generator{[&](auto d, auto n) { output.write(d, n); }} {}
    ~Generator();

    void operator()(std::vector<Command> const& commands);
    void operator()(Command const& command);

    template <typename T, typename... Args>
    void emit(Args&&... args)
    {
        (*this)(T{std::forward<Args>(args)...});
    }

    void flush();

    static std::string generate(std::vector<Command> const& commands)
    {
        auto output = std::string{};
        Generator{[&](auto d, auto n) { output += std::string{d, n}; }}(commands);
        return output;
    }

  private:
    static std::string flush(std::vector<int> _sgr);
    void sgr_add(int _param);

    void write(wchar_t v)
    {
        write(utf8::encode(v));
    }

    void write(utf8::Bytes const& v)
    {
        flush();
        writer_((char const*) &v[0], v.size());
    }

    void write(std::string_view const& _s)
    {
        flush();
        writer_(_s.data(), _s.size());
    }

    template <typename... Args>
    void write(std::string_view const& _s, Args&&... _args)
    {
        write(fmt::format(_s, std::forward<Args>(_args)...));
    }

  private:
    Writer writer_;
    std::vector<int> sgr_;
    Color currentForegroundColor_ = DefaultColor{};
    Color currentBackgroundColor_ = DefaultColor{};
};

}  // namespace terminal