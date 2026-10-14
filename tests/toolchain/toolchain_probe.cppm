// Test-only module: exercises every C++23 and `import std` capability the gfx library depends on.
// A compiler, standard library or CMake upgrade that breaks one of them fails here, with a precise
// test name, instead of somewhere deep inside the library.
export module gfx.tests.toolchain_probe;

import std;

export namespace gfx::tests {

// Compile-time string usable as a template argument (class-type non-type template parameter).
template<std::size_t Size>
struct FixedString {
    std::array<char, Size> characters{};

    consteval FixedString(const char (&text)[Size]) { std::ranges::copy(text, characters.begin()); }

    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view { return {characters.data(), Size - 1}; }
};

template<FixedString Name>
[[nodiscard]] consteval auto name_length() -> std::size_t {
    return Name.view().size();
}

// Reports which branch of `if consteval` was taken.
[[nodiscard]] constexpr auto evaluation_context() -> std::string_view {
    if consteval {
        return "compile-time";
    } else {
        return "run-time";
    }
}

// Value-returning builder chained through an explicit object parameter (deducing this).
struct Accumulator {
    int total = 0;

    [[nodiscard]] constexpr auto add(this Accumulator self, int value) -> Accumulator {
        self.total += value;
        return self;
    }
};

// Failure reported as a value instead of an exception.
[[nodiscard]] auto parse_positive_integer(std::string_view text) -> std::expected<int, std::string> {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::unexpected(std::format("'{}' is not an integer", text));
    }
    if (value <= 0) {
        return std::unexpected(std::format("{} is not strictly positive", value));
    }
    return value;
}

// Sums every element of a two-dimensional view.
[[nodiscard]] auto sum_grid(std::mdspan<const int, std::dextents<std::size_t, 2>> grid) -> int {
    int total = 0;
    for (std::size_t row = 0; row < grid.extent(0); ++row) {
        for (std::size_t column = 0; column < grid.extent(1); ++column) {
            total += grid[row, column];
        }
    }
    return total;
}

// Invokes a callable that can only be moved, never copied.
[[nodiscard]] auto invoke_move_only(std::move_only_function<int()> function) -> int {
    return function();
}

// Returns the line of the caller.
[[nodiscard]] auto caller_line(std::source_location location = std::source_location::current()) -> std::uint_least32_t {
    return location.line();
}

// Formats a value with std::format and also writes it through std::print
// (on MinGW, std::print requires linking libstdc++exp).
[[nodiscard]] auto format_and_print(std::FILE* stream, int value) -> std::string {
    std::print(stream, "toolchain probe value: {}\n", value);
    return std::format("value={:04}", value);
}

// Handle-like value class with a defaulted three-way comparison.
class OrderedValue {
public:
    constexpr explicit OrderedValue(std::uint32_t value) noexcept : m_value{value} {}

    constexpr auto operator<=>(const OrderedValue&) const noexcept = default;

private:
    std::uint32_t m_value;
};

} // namespace gfx::tests
