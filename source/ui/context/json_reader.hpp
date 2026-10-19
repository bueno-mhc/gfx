/// @file json_reader.hpp
/// @brief Bridge from module units to the SAX parser of nlohmann/json (json_reader.cpp, a plain translation unit)
///        for saved UI layouts. It includes no standard header on purpose: a module unit that imports std cannot
///        include <string> or <cstdio> in its global module fragment, and nlohmann/json needs them.
#pragma once

namespace gfx::ui::json
{

/// UTF-8 text: first byte and size in bytes.
struct Text
{
    const char* data = nullptr;
    unsigned long long size = 0;
};

/// Receives a JSON document depth first; a function returning false stops the parse.
class Reader
{
public:
    virtual ~Reader() = default;

    virtual bool null_value() = 0;
    virtual bool boolean(bool value) = 0;
    virtual bool number(double value) = 0;
    virtual bool string(Text value) = 0;
    virtual bool start_object() = 0;
    virtual bool key(Text name) = 0;
    virtual bool end_object() = 0;
    virtual bool start_array() = 0;
    virtual bool end_array() = 0;

    /// The document is malformed at @p byte (offset in the text), as @p message explains.
    virtual void error(unsigned long long byte,
                       Text message) = 0;
};

/// Parses @p text, calling @p reader. Returns whether the document is well-formed (and the reader did not stop
/// the parse).
bool parse(Text text,
           Reader& reader);

} // namespace gfx::ui::json
