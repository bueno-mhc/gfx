// JSON parsing for saved input bindings: nlohmann/json's SAX parser forwarded to json::Reader. A plain
// translation unit (no module), so that nlohmann/json can include the standard headers it needs.
#include "json_reader.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace gfx::platform::json
{

namespace
{

/// Forwards nlohmann/json's SAX events to a Reader.
class SaxAdapter final : public nlohmann::json_sax<nlohmann::json>
{
public:
    explicit SaxAdapter(Reader& reader)
        : m_reader{reader}
    {
    }

    bool null() override
    {
        return m_reader.null_value();
    }

    bool boolean(bool value) override
    {
        return m_reader.boolean(value);
    }

    bool number_integer(number_integer_t value) override
    {
        return m_reader.number(static_cast<double>(value));
    }

    bool number_unsigned(number_unsigned_t value) override
    {
        return m_reader.number(static_cast<double>(value));
    }

    bool number_float(number_float_t value,
                      const string_t& /*text*/) override
    {
        return m_reader.number(value);
    }

    bool string(string_t& value) override
    {
        return m_reader.string(Text{value.data(), value.size()});
    }

    bool binary(binary_t& /*value*/) override
    {
        // Text JSON has no binary values.
        return false;
    }

    bool start_object(std::size_t /*elements*/) override
    {
        return m_reader.start_object();
    }

    bool key(string_t& name) override
    {
        return m_reader.key(Text{name.data(), name.size()});
    }

    bool end_object() override
    {
        return m_reader.end_object();
    }

    bool start_array(std::size_t /*elements*/) override
    {
        return m_reader.start_array();
    }

    bool end_array() override
    {
        return m_reader.end_array();
    }

    bool parse_error(std::size_t position,
                     const std::string& /*last_token*/,
                     const nlohmann::detail::exception& exception) override
    {
        const std::string message = exception.what();
        m_reader.error(position, Text{message.data(), message.size()});
        return false;
    }

private:
    Reader& m_reader;
};

} // namespace

bool
parse(Text text,
      Reader& reader)
{
    // Parse without exceptions: errors reach the reader.
    SaxAdapter adapter{reader};
    const char* const begin = text.data;
    const char* const end = text.data + text.size;
    return nlohmann::json::sax_parse(begin, end, &adapter, nlohmann::json::input_format_t::json, true, false);
}

} // namespace gfx::platform::json
