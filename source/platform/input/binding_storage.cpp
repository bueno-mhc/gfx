// Saved bindings: written as JSON here, read through nlohmann/json's SAX parser (json_reader.hpp) into a
// small document, then checked field by field so that every problem is reported with its path.
//
// {
//   "format": "gfx.input.bindings",
//   "version": 1,
//   "players": [
//     {
//       "player": 0,
//       "bindings": [
//         {
//           "context": "gameplay",
//           "action": "jump",
//           "bindings": [
//             {"kind": "BUTTON", "sources": [{"KEY": "SPACE"}]},
//             {"kind": "CHORD", "sources": [{"KEY_CODE": "Z"}], "modifiers": "LEFT_CONTROL | RIGHT_CONTROL", "exact_modifiers": true}
//           ]
//         }
//       ]
//     }
//   ]
// }
//
// Fields equal to their default are omitted; unknown fields are errors (version 1 has no extensions).
module;

#include "json_reader.hpp"

module gfx.platform.input;

import std;
import gfx.core;
import gfx.platform.enums;
import gfx.platform.handles;

namespace gfx::platform
{

namespace
{

/// Value of the "format" field.
constexpr std::string_view FORMAT_NAME = "gfx.input.bindings";

// ================================================================================================= Writing

/// @p text as a JSON string literal.
[[nodiscard]] auto
json_string(std::string_view text) -> std::string
{
    std::string result = "\"";
    for (const char character : text)
    {
        switch (character)
        {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\r':
                result += "\\r";
                break;
            default:
                if (static_cast<unsigned char>(character) < 0x20U)
                {
                    result += std::format("\\u{:04X}", static_cast<unsigned int>(static_cast<unsigned char>(character)));
                }
                else
                {
                    result += character;
                }
                break;
        }
    }
    result += '"';
    return result;
}

/// Name of the enumerator @p source names: its spelling, or "U+XXXX" for a key code without one.
[[nodiscard]] auto
source_name(InputSource source) -> std::string
{
    switch (source.kind)
    {
        case InputSourceKind::NONE:
            return "NONE";
        case InputSourceKind::KEY:
            return std::string{to_string(static_cast<Key>(source.code))};
        case InputSourceKind::KEY_CODE:
        {
            const std::string_view name = to_string(static_cast<KeyCode>(source.code));
            return name != "<invalid>" ? std::string{name} : std::format("U+{:04X}", source.code);
        }
        case InputSourceKind::MOUSE_BUTTON:
            return std::string{to_string(static_cast<MouseButton>(source.code))};
        case InputSourceKind::MOUSE_AXIS:
            return std::string{to_string(static_cast<MouseAxis>(source.code))};
        case InputSourceKind::GAMEPAD_BUTTON:
            return std::string{to_string(static_cast<GamepadButton>(source.code))};
        case InputSourceKind::GAMEPAD_AXIS:
            return std::string{to_string(static_cast<GamepadAxis>(source.code))};
    }
    return "NONE";
}

/// @p binding as a JSON object (fields equal to their default omitted).
[[nodiscard]] auto
binding_json(const Binding& binding) -> std::string
{
    // Kind and sources always.
    const Binding defaults;
    std::string text = std::format("{{\"kind\": {}, \"sources\": [", json_string(to_string(binding.kind)));
    for (std::size_t index = 0; index < binding.source_count(); ++index)
    {
        const InputSource source = binding.sources[index];
        text += std::format("{}{{{}: {}}}", index == 0 ? "" : ", ", json_string(to_string(source.kind)), json_string(source_name(source)));
    }
    text += "]";

    // Every other field when it differs from the default.
    if (binding.modifiers != defaults.modifiers)
    {
        text += std::format(", \"modifiers\": {}", json_string(to_string(binding.modifiers)));
    }
    if (binding.exact_modifiers != defaults.exact_modifiers)
    {
        text += std::format(", \"exact_modifiers\": {}", binding.exact_modifiers);
    }
    if (binding.deadzone != defaults.deadzone)
    {
        text += std::format(", \"deadzone\": {{\"kind\": {}, \"lower\": {}, \"upper\": {}}}", json_string(to_string(binding.deadzone)),
                            binding.deadzone_lower, binding.deadzone_upper);
    }
    if (binding.curve != defaults.curve)
    {
        text += std::format(", \"curve\": {{\"kind\": {}, \"exponent\": {}}}", json_string(to_string(binding.curve)), binding.curve_exponent);
    }
    if (binding.invert_x != defaults.invert_x)
    {
        text += std::format(", \"invert_x\": {}", binding.invert_x);
    }
    if (binding.invert_y != defaults.invert_y)
    {
        text += std::format(", \"invert_y\": {}", binding.invert_y);
    }
    if (binding.scale != defaults.scale)
    {
        text += std::format(", \"scale\": {{\"x\": {}, \"y\": {}}}", binding.scale.x, binding.scale.y);
    }
    if (binding.swizzle != defaults.swizzle)
    {
        text += std::format(", \"swizzle\": {}", binding.swizzle);
    }
    if (binding.normalize != defaults.normalize)
    {
        text += std::format(", \"normalize\": {}", binding.normalize);
    }
    if (binding.trigger != defaults.trigger || binding.trigger_time != defaults.trigger_time)
    {
        text += std::format(", \"trigger\": {{\"kind\": {}, \"milliseconds\": {}}}", json_string(to_string(binding.trigger)),
                            binding.trigger_time.count());
    }
    text += "}";
    return text;
}

// ================================================================================================= Document

struct JsonValue;

/// A JSON array.
using JsonArray = std::vector<JsonValue>;

/// A JSON object: members in document order.
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

/// A JSON value.
struct JsonValue
{
    std::variant<std::monostate, bool, double, std::string, JsonArray, JsonObject> value{};
};

/// Builds a JsonValue from the parser's events.
class DocumentBuilder final : public json::Reader
{
public:
    bool null_value() override
    {
        return place(JsonValue{}) != nullptr;
    }

    bool boolean(bool value) override
    {
        return place(JsonValue{value}) != nullptr;
    }

    bool number(double value) override
    {
        return place(JsonValue{value}) != nullptr;
    }

    bool string(json::Text value) override
    {
        return place(JsonValue{std::string{value.data, static_cast<std::size_t>(value.size)}}) != nullptr;
    }

    bool start_object() override
    {
        m_stack.push_back(place(JsonValue{JsonObject{}}));
        return true;
    }

    bool key(json::Text name) override
    {
        m_key.assign(name.data, static_cast<std::size_t>(name.size));
        return true;
    }

    bool end_object() override
    {
        m_stack.pop_back();
        return true;
    }

    bool start_array() override
    {
        m_stack.push_back(place(JsonValue{JsonArray{}}));
        return true;
    }

    bool end_array() override
    {
        m_stack.pop_back();
        return true;
    }

    void error(unsigned long long byte,
               json::Text message) override
    {
        m_error = std::format("malformed JSON at byte {}: {}", byte, std::string_view{message.data, static_cast<std::size_t>(message.size)});
    }

    /// The document.
    [[nodiscard]] auto root() const noexcept -> const JsonValue&
    {
        return m_root;
    }

    /// Why parsing failed.
    [[nodiscard]] auto error_message() const noexcept -> std::string_view
    {
        return m_error;
    }

private:
    /// Stores @p value in the innermost array or object (or as the root).
    auto place(JsonValue value) -> JsonValue*
    {
        if (m_stack.empty())
        {
            m_root = std::move(value);
            return &m_root;
        }
        if (auto* array = std::get_if<JsonArray>(&m_stack.back()->value))
        {
            return &array->emplace_back(std::move(value));
        }
        auto& object = std::get<JsonObject>(m_stack.back()->value);
        return &object.emplace_back(m_key, std::move(value)).second;
    }

private:
    JsonValue m_root{};
    std::vector<JsonValue*> m_stack{};
    std::string m_key{};
    std::string m_error{};
};

// ================================================================================================= Reading

/// Reads the document's fields, collecting every problem with the path of the field.
class StorageReader
{
public:
    explicit StorageReader(std::vector<std::string>& problems)
        : m_problems{problems}
    {
    }

    /// The lists of document @p root.
    [[nodiscard]] auto read(const JsonValue& root) -> std::vector<StoredBindingList>
    {
        // Header: format and version.
        std::vector<StoredBindingList> lists;
        const JsonObject* document = object(root, "the document");
        if (document == nullptr)
        {
            return lists;
        }
        check_members(*document, "the document", {"format", "version", "players"});
        const std::string* format = text(member(*document, "format", "the document"), "format");
        if (format != nullptr && *format != FORMAT_NAME)
        {
            problem("format", std::format("is \"{}\", not \"{}\"", *format, FORMAT_NAME));
        }
        const std::optional<std::int64_t> version = integer(member(*document, "version", "the document"), "version");
        if (version && *version != BINDING_STORAGE_VERSION)
        {
            problem("version", std::format("is {}; this build reads version {}", *version, BINDING_STORAGE_VERSION));
            return lists;
        }

        // Players, their lists, and the bindings of each list.
        const JsonArray* players = array(member(*document, "players", "the document"), "players");
        for (std::size_t player_index = 0; players != nullptr && player_index < players->size(); ++player_index)
        {
            const std::string player_path = std::format("players[{}]", player_index);
            const JsonObject* player = object((*players)[player_index], player_path);
            if (player == nullptr)
            {
                continue;
            }
            check_members(*player, player_path, {"player", "bindings"});
            const std::optional<std::int64_t> number = integer(member(*player, "player", player_path), player_path + ".player");
            if (number && (*number < 0 || *number >= MAXIMUM_PLAYERS))
            {
                problem(player_path + ".player", std::format("is {}, not a player from 0 to {}", *number, MAXIMUM_PLAYERS - 1));
            }
            const JsonArray* entries = array(member(*player, "bindings", player_path), player_path + ".bindings");
            for (std::size_t entry_index = 0; entries != nullptr && entry_index < entries->size(); ++entry_index)
            {
                const std::string entry_path = std::format("{}.bindings[{}]", player_path, entry_index);
                std::optional<StoredBindingList> list = read_list((*entries)[entry_index], entry_path);
                if (list && number && *number >= 0 && *number < MAXIMUM_PLAYERS)
                {
                    list->player = static_cast<std::uint32_t>(*number);
                    lists.push_back(std::move(*list));
                }
            }
        }
        return lists;
    }

private:
    /// Records a problem of the field at @p path.
    void problem(std::string_view path,
                 std::string_view what)
    {
        m_problems.push_back(std::format("{} {}", path, what));
    }

    /// The member @p name of @p object (null, after recording a problem, when it is missing).
    [[nodiscard]] auto member(const JsonObject& object,
                              std::string_view name,
                              std::string_view path) -> const JsonValue*
    {
        const auto found = std::ranges::find(object, name, &std::pair<std::string, JsonValue>::first);
        if (found == object.end())
        {
            problem(path, std::format("lacks the field \"{}\"", name));
            return nullptr;
        }
        return &found->second;
    }

    /// The member @p name of @p object, or null when it is absent (optional fields).
    [[nodiscard]] static auto optional_member(const JsonObject& object,
                                              std::string_view name) -> const JsonValue*
    {
        const auto found = std::ranges::find(object, name, &std::pair<std::string, JsonValue>::first);
        return found != object.end() ? &found->second : nullptr;
    }

    /// Records the members of @p object that are not in @p known.
    void check_members(const JsonObject& object,
                       std::string_view path,
                       std::initializer_list<std::string_view> known)
    {
        for (const auto& [name, value] : object)
        {
            if (std::ranges::find(known, name) == known.end())
            {
                problem(path, std::format("has an unknown field \"{}\"", name));
            }
        }
    }

    [[nodiscard]] auto object(const JsonValue& value,
                              std::string_view path) -> const JsonObject*
    {
        if (const auto* result = std::get_if<JsonObject>(&value.value))
        {
            return result;
        }
        problem(path, "is not an object");
        return nullptr;
    }

    [[nodiscard]] auto array(const JsonValue* value,
                             std::string_view path) -> const JsonArray*
    {
        if (value == nullptr)
        {
            return nullptr;
        }
        if (const auto* result = std::get_if<JsonArray>(&value->value))
        {
            return result;
        }
        problem(path, "is not an array");
        return nullptr;
    }

    [[nodiscard]] auto text(const JsonValue* value,
                            std::string_view path) -> const std::string*
    {
        if (value == nullptr)
        {
            return nullptr;
        }
        if (const auto* result = std::get_if<std::string>(&value->value))
        {
            return result;
        }
        problem(path, "is not a string");
        return nullptr;
    }

    [[nodiscard]] auto number(const JsonValue* value,
                              std::string_view path) -> std::optional<double>
    {
        if (value == nullptr)
        {
            return std::nullopt;
        }
        if (const auto* result = std::get_if<double>(&value->value))
        {
            return *result;
        }
        problem(path, "is not a number");
        return std::nullopt;
    }

    [[nodiscard]] auto integer(const JsonValue* value,
                               std::string_view path) -> std::optional<std::int64_t>
    {
        const std::optional<double> result = number(value, path);
        if (result && (std::trunc(*result) != *result || std::abs(*result) > 9.0e15))
        {
            problem(path, std::format("is {}, not an integer", *result));
            return std::nullopt;
        }
        return result ? std::optional<std::int64_t>{static_cast<std::int64_t>(*result)} : std::nullopt;
    }

    [[nodiscard]] auto boolean(const JsonValue* value,
                               std::string_view path) -> std::optional<bool>
    {
        if (value == nullptr)
        {
            return std::nullopt;
        }
        if (const auto* result = std::get_if<bool>(&value->value))
        {
            return *result;
        }
        problem(path, "is not true or false");
        return std::nullopt;
    }

    /// The enumerator of @p Enumeration a string field names.
    template<class Enumeration>
    [[nodiscard]] auto enumerator(const JsonValue* value,
                                  std::string_view path) -> std::optional<Enumeration>
    {
        const std::string* name = text(value, path);
        if (name == nullptr)
        {
            return std::nullopt;
        }
        if (const std::optional<Enumeration> result = from_string<Enumeration>(*name))
        {
            return result;
        }
        problem(path, std::format("names no {} (\"{}\")", std::same_as<Enumeration, KeyModifier> ? "set of modifiers" : "enumerator", *name));
        return std::nullopt;
    }

    /// A source object: {"KIND": "NAME"}.
    [[nodiscard]] auto read_source(const JsonValue& value,
                                   std::string_view path) -> std::optional<InputSource>
    {
        const JsonObject* source = object(value, path);
        if (source == nullptr)
        {
            return std::nullopt;
        }
        if (source->size() != 1)
        {
            problem(path, "must have exactly one field: the kind of input naming the input, e.g. {\"KEY\": \"SPACE\"}");
            return std::nullopt;
        }
        const auto& [kind_name, name_value] = source->front();
        const std::optional<InputSourceKind> kind = from_string<InputSourceKind>(kind_name);
        const std::string* name = text(&name_value, path);
        if (!kind || *kind == InputSourceKind::NONE)
        {
            problem(path, std::format("names no kind of input (\"{}\")", kind_name));
            return std::nullopt;
        }
        if (name == nullptr)
        {
            return std::nullopt;
        }

        // The enumerator of the kind, or a code point for key codes ("U+20AC").
        std::optional<std::uint32_t> code;
        switch (*kind)
        {
            case InputSourceKind::KEY:
                code = from_string<Key>(*name).transform([](Key key) { return std::uint32_t{std::to_underlying(key)}; });
                break;
            case InputSourceKind::KEY_CODE:
                code = from_string<KeyCode>(*name).transform([](KeyCode key) { return std::to_underlying(key); });
                if (!code && name->starts_with("U+") && name->size() > 2)
                {
                    std::uint32_t parsed = 0;
                    const auto [end, error] = std::from_chars(name->data() + 2, name->data() + name->size(), parsed, 16);
                    if (error == std::errc{} && end == name->data() + name->size())
                    {
                        code = parsed;
                    }
                }
                break;
            case InputSourceKind::MOUSE_BUTTON:
                code = from_string<MouseButton>(*name).transform([](MouseButton button) { return std::uint32_t{std::to_underlying(button)}; });
                break;
            case InputSourceKind::MOUSE_AXIS:
                code = from_string<MouseAxis>(*name).transform([](MouseAxis axis) { return std::uint32_t{std::to_underlying(axis)}; });
                break;
            case InputSourceKind::GAMEPAD_BUTTON:
                code = from_string<GamepadButton>(*name).transform([](GamepadButton button) { return std::uint32_t{std::to_underlying(button)}; });
                break;
            case InputSourceKind::GAMEPAD_AXIS:
                code = from_string<GamepadAxis>(*name).transform([](GamepadAxis axis) { return std::uint32_t{std::to_underlying(axis)}; });
                break;
            case InputSourceKind::NONE:
                break;
        }
        if (!code)
        {
            problem(path, std::format("names no {} input (\"{}\")", kind_name, *name));
            return std::nullopt;
        }
        return InputSource{.kind = *kind, .code = *code};
    }

    /// A binding object.
    [[nodiscard]] auto read_binding(const JsonValue& value,
                                    std::string_view path) -> std::optional<Binding>
    {
        // Kind and sources.
        const JsonObject* fields = object(value, path);
        if (fields == nullptr)
        {
            return std::nullopt;
        }
        const std::size_t problems_before = m_problems.size();
        check_members(*fields, path,
                      {"kind", "sources", "modifiers", "exact_modifiers", "deadzone", "curve", "invert_x", "invert_y", "scale", "swizzle",
                       "normalize", "trigger"});
        Binding binding;
        const std::string base{path};
        binding.kind = enumerator<BindingKind>(member(*fields, "kind", path), base + ".kind").value_or(BindingKind::NONE);
        const JsonArray* sources = array(member(*fields, "sources", path), base + ".sources");
        if (sources != nullptr && sources->size() > MAXIMUM_BINDING_SOURCES)
        {
            problem(base + ".sources", std::format("has {} sources; a binding reads at most {}", sources->size(), MAXIMUM_BINDING_SOURCES));
        }
        for (std::size_t index = 0; sources != nullptr && index < std::min(sources->size(), MAXIMUM_BINDING_SOURCES); ++index)
        {
            binding.sources[index] = read_source((*sources)[index], std::format("{}.sources[{}]", base, index)).value_or(InputSource{});
        }

        // Optional fields.
        if (const JsonValue* modifiers = optional_member(*fields, "modifiers"))
        {
            binding.modifiers = enumerator<KeyModifier>(modifiers, base + ".modifiers").value_or(KeyModifier::NONE);
        }
        binding.exact_modifiers = boolean(optional_member(*fields, "exact_modifiers"), base + ".exact_modifiers").value_or(false);
        if (const JsonValue* deadzone = optional_member(*fields, "deadzone"))
        {
            const std::string deadzone_path = base + ".deadzone";
            if (const JsonObject* parts = object(*deadzone, deadzone_path))
            {
                check_members(*parts, deadzone_path, {"kind", "lower", "upper"});
                binding.deadzone = enumerator<DeadzoneKind>(member(*parts, "kind", deadzone_path), deadzone_path + ".kind").value_or(DeadzoneKind::NONE);
                binding.deadzone_lower = static_cast<float>(number(member(*parts, "lower", deadzone_path), deadzone_path + ".lower").value_or(0.0));
                binding.deadzone_upper = static_cast<float>(number(member(*parts, "upper", deadzone_path), deadzone_path + ".upper").value_or(1.0));
            }
        }
        if (const JsonValue* curve = optional_member(*fields, "curve"))
        {
            const std::string curve_path = base + ".curve";
            if (const JsonObject* parts = object(*curve, curve_path))
            {
                check_members(*parts, curve_path, {"kind", "exponent"});
                binding.curve = enumerator<ResponseCurve>(member(*parts, "kind", curve_path), curve_path + ".kind").value_or(ResponseCurve::LINEAR);
                binding.curve_exponent = static_cast<float>(number(member(*parts, "exponent", curve_path), curve_path + ".exponent").value_or(1.0));
            }
        }
        binding.invert_x = boolean(optional_member(*fields, "invert_x"), base + ".invert_x").value_or(false);
        binding.invert_y = boolean(optional_member(*fields, "invert_y"), base + ".invert_y").value_or(false);
        if (const JsonValue* scale = optional_member(*fields, "scale"))
        {
            const std::string scale_path = base + ".scale";
            if (const JsonObject* parts = object(*scale, scale_path))
            {
                check_members(*parts, scale_path, {"x", "y"});
                binding.scale.x = static_cast<float>(number(member(*parts, "x", scale_path), scale_path + ".x").value_or(1.0));
                binding.scale.y = static_cast<float>(number(member(*parts, "y", scale_path), scale_path + ".y").value_or(1.0));
            }
        }
        binding.swizzle = boolean(optional_member(*fields, "swizzle"), base + ".swizzle").value_or(false);
        binding.normalize = boolean(optional_member(*fields, "normalize"), base + ".normalize").value_or(false);
        if (const JsonValue* trigger = optional_member(*fields, "trigger"))
        {
            const std::string trigger_path = base + ".trigger";
            if (const JsonObject* parts = object(*trigger, trigger_path))
            {
                check_members(*parts, trigger_path, {"kind", "milliseconds"});
                binding.trigger = enumerator<TriggerKind>(member(*parts, "kind", trigger_path), trigger_path + ".kind").value_or(TriggerKind::DOWN);
                binding.trigger_time = std::chrono::milliseconds{
                    integer(member(*parts, "milliseconds", trigger_path), trigger_path + ".milliseconds").value_or(0)};
            }
        }
        if (m_problems.size() != problems_before)
        {
            return std::nullopt;
        }
        return binding;
    }

    /// A list object: {"context": ..., "action": ..., "bindings": [...]}.
    [[nodiscard]] auto read_list(const JsonValue& value,
                                 std::string_view path) -> std::optional<StoredBindingList>
    {
        const JsonObject* fields = object(value, path);
        if (fields == nullptr)
        {
            return std::nullopt;
        }
        const std::size_t problems_before = m_problems.size();
        const std::string base{path};
        check_members(*fields, path, {"context", "action", "bindings"});
        StoredBindingList list;
        if (const std::string* context = text(member(*fields, "context", path), base + ".context"))
        {
            list.context = *context;
        }
        if (const std::string* action = text(member(*fields, "action", path), base + ".action"))
        {
            list.action = *action;
        }
        const JsonArray* bindings = array(member(*fields, "bindings", path), base + ".bindings");
        for (std::size_t index = 0; bindings != nullptr && index < bindings->size(); ++index)
        {
            if (std::optional<Binding> binding = read_binding((*bindings)[index], std::format("{}.bindings[{}]", base, index)))
            {
                list.bindings.push_back(*binding);
            }
        }
        if (m_problems.size() != problems_before)
        {
            return std::nullopt;
        }
        return list;
    }

private:
    std::vector<std::string>& m_problems;
};

} // namespace

auto
write_binding_storage(std::span<const StoredBindingList> lists) -> std::string
{
    // Header, then players in order with their lists.
    std::string text = std::format("{{\n  \"format\": {},\n  \"version\": {},\n  \"players\": [", json_string(FORMAT_NAME), BINDING_STORAGE_VERSION);
    bool first_player = true;
    for (std::size_t index = 0; index < lists.size();)
    {
        const std::uint32_t player = lists[index].player;
        text += std::format("{}\n    {{\n      \"player\": {},\n      \"bindings\": [", first_player ? "" : ",", player);
        first_player = false;
        bool first_list = true;
        for (; index < lists.size() && lists[index].player == player; ++index)
        {
            const StoredBindingList& list = lists[index];
            text += std::format("{}\n        {{\n          \"context\": {},\n          \"action\": {},\n          \"bindings\": [",
                                first_list ? "" : ",", json_string(list.context), json_string(list.action));
            first_list = false;
            for (std::size_t binding = 0; binding < list.bindings.size(); ++binding)
            {
                text += std::format("{}\n            {}", binding == 0 ? "" : ",", binding_json(list.bindings[binding]));
            }
            text += list.bindings.empty() ? "]\n        }" : "\n          ]\n        }";
        }
        text += "\n      ]\n    }";
    }
    text += lists.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return text;
}

auto
read_binding_storage(std::string_view json,
                     std::vector<std::string>& problems) -> std::vector<StoredBindingList>
{
    // Parse into a document, then read it.
    DocumentBuilder builder;
    if (!json::parse(json::Text{json.data(), json.size()}, builder))
    {
        problems.push_back(builder.error_message().empty() ? std::string{"malformed JSON"} : std::string{builder.error_message()});
        return {};
    }
    StorageReader reader{problems};
    return reader.read(builder.root());
}

} // namespace gfx::platform
