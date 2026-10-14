// Defect detector (with import_std_first_formatter.cpp): see that file.
import std;

auto format_first(int value) -> std::string;

auto main() -> int {
    const std::string text = std::format("{}{}", format_first(1), std::string{"second"});
    return text.empty() ? 1 : 0;
}
