// Defect detector (with import_std_second_formatter.cpp): two object files formatting strings
// through `import std` define the same static local variable as a strong symbol on MinGW.
import std;

auto format_first(int value) -> std::string {
    return std::format("[{}]", std::to_string(value));
}
