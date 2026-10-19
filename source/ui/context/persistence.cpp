// Saved layouts (UiContext::save_layout(), load_layout()): written as JSON here, read through nlohmann/json's SAX
// parser (json_reader.hpp) into a small document, checked field by field (every problem with its path), then applied
// at once (all or nothing).
//
// {
//   "format": "gfx.ui.layout",
//   "version": 1,
//   "windows": [
//     {"id": "0x1A2B3C4D", "name": "Inspector", "position": [40, 60], "size": [320, 480], "collapsed": false}
//   ],
//   "dock_trees": [
//     {"dock_space": "0x5E6F7081", "root": {"split": "LEFT_RIGHT", "ratio": 0.25, "children": [
//         {"windows": ["0x1A2B3C4D"], "selected": "0x1A2B3C4D"},
//         {"windows": [], "central": true}]}},
//     {"position": [500, 80], "size": [300, 200], "root": {"windows": ["0x0BADF00D", "0x12345678"], "selected": "0x12345678"}}
//   ],
//   "tables": [
//     {"id": "0x9ABCDEF0", "columns": [{"width": 80, "weight": -1, "order": 0, "visible": true, "sort_order": 0, "sort_direction": "ASCENDING"}]}
//   ]
// }
//
// Identifiers are written in hexadecimal; a dock tree has a "dock_space" (its dock space's identifier) or, floating,
// its host's "position" and "size". Unknown fields are errors (version 1 has no extensions).
module;

#include "json_reader.hpp"

module gfx.ui.context;

import std;
import gfx.core.enums;
import gfx.core.types;
import gfx.ui.enums;
import gfx.ui.errors;
import gfx.ui.handles;
import gfx.ui.draw;
import :state;

namespace gfx::ui
{

namespace
{

constexpr std::uint32_t NONE = DockNodeState::NONE;

// ================================================================================================= Writing

/// @p value as a JSON string (quotes, backslashes and control characters escaped).
auto
json_string(std::string_view value) -> std::string
{
    std::string text = "\"";
    for (const char character : value)
    {
        switch (character)
        {
            case '"':  text += "\\\""; break;
            case '\\': text += "\\\\"; break;
            case '\n': text += "\\n"; break;
            case '\r': text += "\\r"; break;
            case '\t': text += "\\t"; break;
            default:
                if (static_cast<unsigned char>(character) < 0x20)
                {
                    text += std::format("\\u{:04X}", static_cast<unsigned int>(character));
                }
                else
                {
                    text += character;
                }
                break;
        }
    }
    text += '"';
    return text;
}

/// @p id as a JSON string "0x...".
auto
json_identifier(std::uint32_t id) -> std::string
{
    return std::format("\"0x{:08X}\"", id);
}

/// Node @p node's subtree as JSON.
auto
node_json(const ContextState& state,
          std::uint32_t node,
          std::uint32_t depth) -> std::string
{
    const DockNodeState& entry = state.dock_nodes[node];
    if (entry.split() && depth < 64)
    {
        return std::format("{{\"split\": \"{}\", \"ratio\": {}, \"children\": [{}, {}]}}", entry.side_by_side ? "LEFT_RIGHT" : "TOP_BOTTOM", entry.ratio,
                           node_json(state, entry.children[0], depth + 1), node_json(state, entry.children[1], depth + 1));
    }
    std::string text = "{\"windows\": [";
    for (std::size_t window = 0; window < entry.windows.size(); ++window)
    {
        text += std::format("{}{}", window == 0 ? "" : ", ", json_identifier(entry.windows[window]));
    }
    text += "]";
    if (entry.selected != 0)
    {
        text += std::format(", \"selected\": {}", json_identifier(entry.selected));
    }
    if (entry.central)
    {
        text += ", \"central\": true";
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

    JsonValue m_root{};
    std::vector<JsonValue*> m_stack{};
    std::string m_key{};
    std::string m_error{};
};

// ================================================================================================= Reading

/// A saved window.
struct LoadedWindow
{
    std::uint32_t id = 0;
    std::string name{};
    Vector2 position{};
    Vector2 size{};
    bool collapsed = false;
};

/// A saved dock node (children by index in LoadedTree::nodes).
struct LoadedNode
{
    bool split = false;
    bool side_by_side = true;
    float ratio = 0.5F;
    std::array<std::size_t, 2> children{};
    std::vector<std::uint32_t> windows{};
    std::uint32_t selected = 0;
    bool central = false;
};

/// A saved dock tree (its root is nodes[0]).
struct LoadedTree
{
    std::uint32_t dock_space = 0;
    Vector2 position{};
    Vector2 size{};
    std::vector<LoadedNode> nodes{};
};

/// A saved table column.
struct LoadedColumn
{
    float width = -1;
    float weight = -1;
    std::uint32_t order = 0;
    bool visible = true;
    std::int32_t sort_order = -1;
    SortDirection sort_direction = SortDirection::NONE;
};

/// A saved table.
struct LoadedTable
{
    std::uint32_t id = 0;
    std::vector<LoadedColumn> columns{};
};

/// A saved layout.
struct LoadedLayout
{
    std::vector<LoadedWindow> windows{};
    std::vector<LoadedTree> trees{};
    std::vector<LoadedTable> tables{};
};

/// Reads the document's fields, collecting every problem with the path of the field.
class LayoutReader
{
public:
    explicit LayoutReader(std::vector<std::string>& problems)
        : m_problems{problems}
    {
    }

    /// The layout of document @p root.
    [[nodiscard]] auto read(const JsonValue& root) -> LoadedLayout
    {
        // Header: format and version.
        LoadedLayout layout{};
        const JsonObject* document = object(root, "$");
        if (document == nullptr)
        {
            return layout;
        }
        check_members(*document, "$", {"format", "version", "windows", "dock_trees", "tables"});
        const std::string* format = text(member(*document, "format", "$"), "$.format");
        if (format != nullptr && *format != LAYOUT_FORMAT)
        {
            problem("$.format", std::format("is \"{}\", not \"{}\"", *format, LAYOUT_FORMAT));
        }
        const std::optional<std::int64_t> version = integer(member(*document, "version", "$"), "$.version");
        if (version && *version != LAYOUT_FORMAT_VERSION)
        {
            problem("$.version", std::format("is {}; this build reads version {}", *version, LAYOUT_FORMAT_VERSION));
            return layout;
        }

        // Windows.
        const JsonArray* windows = array(optional_member(*document, "windows"), "$.windows");
        for (std::size_t index = 0; windows != nullptr && index < windows->size(); ++index)
        {
            const std::string path = std::format("$.windows[{}]", index);
            const JsonObject* entry = object((*windows)[index], path);
            if (entry == nullptr)
            {
                continue;
            }
            check_members(*entry, path, {"id", "name", "position", "size", "collapsed"});
            LoadedWindow window{};
            window.id = identifier(member(*entry, "id", path), path + ".id").value_or(0);
            if (const std::string* name = text(optional_member(*entry, "name"), path + ".name"))
            {
                window.name = *name;
            }
            window.position = vector(member(*entry, "position", path), path + ".position").value_or(Vector2{});
            window.size = vector(member(*entry, "size", path), path + ".size").value_or(Vector2{});
            if (!(window.size.x > 0 && window.size.y > 0) && window.id != 0)
            {
                problem(path + ".size", "is not positive");
            }
            window.collapsed = boolean(optional_member(*entry, "collapsed"), path + ".collapsed").value_or(false);
            layout.windows.push_back(std::move(window));
        }

        // Dock trees.
        const JsonArray* trees = array(optional_member(*document, "dock_trees"), "$.dock_trees");
        std::vector<std::uint32_t> dock_spaces{};
        std::vector<std::uint32_t> docked{};
        for (std::size_t index = 0; trees != nullptr && index < trees->size(); ++index)
        {
            const std::string path = std::format("$.dock_trees[{}]", index);
            const JsonObject* entry = object((*trees)[index], path);
            if (entry == nullptr)
            {
                continue;
            }
            check_members(*entry, path, {"dock_space", "position", "size", "root"});
            LoadedTree tree{};
            if (const JsonValue* dock_space = optional_member(*entry, "dock_space"))
            {
                tree.dock_space = identifier(dock_space, path + ".dock_space").value_or(0);
                if (std::ranges::contains(dock_spaces, tree.dock_space))
                {
                    problem(path + ".dock_space", "names a dock space of an earlier tree");
                }
                dock_spaces.push_back(tree.dock_space);
            }
            else
            {
                tree.position = vector(member(*entry, "position", path), path + ".position").value_or(Vector2{});
                tree.size = vector(member(*entry, "size", path), path + ".size").value_or(Vector2{});
                if (!(tree.size.x > 0 && tree.size.y > 0))
                {
                    problem(path + ".size", "is not positive");
                }
            }
            if (const JsonValue* root_node = member(*entry, "root", path))
            {
                read_node(*root_node, path + ".root", tree, docked, 0);
            }
            layout.trees.push_back(std::move(tree));
        }

        // Tables.
        const JsonArray* tables = array(optional_member(*document, "tables"), "$.tables");
        for (std::size_t index = 0; tables != nullptr && index < tables->size(); ++index)
        {
            const std::string path = std::format("$.tables[{}]", index);
            const JsonObject* entry = object((*tables)[index], path);
            if (entry == nullptr)
            {
                continue;
            }
            check_members(*entry, path, {"id", "columns"});
            LoadedTable table{};
            table.id = identifier(member(*entry, "id", path), path + ".id").value_or(0);
            const JsonArray* columns = array(member(*entry, "columns", path), path + ".columns");
            if (columns != nullptr && (columns->empty() || columns->size() > MAXIMUM_TABLE_COLUMNS))
            {
                problem(path + ".columns", std::format("has {} columns, not 1 to {}", columns->size(), MAXIMUM_TABLE_COLUMNS));
                continue;
            }
            std::vector<bool> orders(columns != nullptr ? columns->size() : 0, false);
            for (std::size_t column = 0; columns != nullptr && column < columns->size(); ++column)
            {
                const std::string column_path = std::format("{}.columns[{}]", path, column);
                const JsonObject* fields = object((*columns)[column], column_path);
                if (fields == nullptr)
                {
                    continue;
                }
                check_members(*fields, column_path, {"width", "weight", "order", "visible", "sort_order", "sort_direction"});
                LoadedColumn loaded{};
                loaded.width = static_cast<float>(number(member(*fields, "width", column_path), column_path + ".width").value_or(-1.0));
                loaded.weight = static_cast<float>(number(member(*fields, "weight", column_path), column_path + ".weight").value_or(-1.0));
                const std::int64_t order = integer(member(*fields, "order", column_path), column_path + ".order").value_or(0);
                if (order < 0 || order >= static_cast<std::int64_t>(columns->size()) || orders[static_cast<std::size_t>(order)])
                {
                    problem(column_path + ".order", std::format("is {}: each column needs its own place from 0 to {}", order, columns->size() - 1));
                }
                else
                {
                    orders[static_cast<std::size_t>(order)] = true;
                    loaded.order = static_cast<std::uint32_t>(order);
                }
                loaded.visible = boolean(member(*fields, "visible", column_path), column_path + ".visible").value_or(true);
                const std::int64_t sort_order = integer(member(*fields, "sort_order", column_path), column_path + ".sort_order").value_or(-1);
                loaded.sort_order = static_cast<std::int32_t>(std::clamp<std::int64_t>(sort_order, -1, MAXIMUM_TABLE_COLUMNS));
                if (const std::string* direction = text(member(*fields, "sort_direction", column_path), column_path + ".sort_direction"))
                {
                    if (const std::optional<SortDirection> parsed = from_string<SortDirection>(*direction))
                    {
                        loaded.sort_direction = *parsed;
                    }
                    else
                    {
                        problem(column_path + ".sort_direction", std::format("is \"{}\", not NONE, ASCENDING or DESCENDING", *direction));
                    }
                }
                table.columns.push_back(loaded);
            }
            layout.tables.push_back(std::move(table));
        }
        return layout;
    }

private:
    /// Node @p value of @p tree at @p path, appended to its nodes (its children after it).
    auto read_node(const JsonValue& value,
                   const std::string& path,
                   LoadedTree& tree,
                   std::vector<std::uint32_t>& docked,
                   std::uint32_t depth) -> std::size_t
    {
        const std::size_t index = tree.nodes.size();
        tree.nodes.emplace_back();
        const JsonObject* entry = object(value, path);
        if (entry == nullptr)
        {
            return index;
        }
        if (depth > 32)
        {
            problem(path, "is nested too deeply");
            return index;
        }
        if (optional_member(*entry, "split") != nullptr)
        {
            check_members(*entry, path, {"split", "ratio", "children"});
            const std::string* split = text(member(*entry, "split", path), path + ".split");
            if (split != nullptr && *split != "LEFT_RIGHT" && *split != "TOP_BOTTOM")
            {
                problem(path + ".split", std::format("is \"{}\", not LEFT_RIGHT or TOP_BOTTOM", *split));
            }
            const double ratio = number(member(*entry, "ratio", path), path + ".ratio").value_or(0.5);
            if (!(ratio > 0.0 && ratio < 1.0))
            {
                problem(path + ".ratio", std::format("is {}, not between 0 and 1", ratio));
            }
            const JsonArray* children = array(member(*entry, "children", path), path + ".children");
            if (children != nullptr && children->size() != 2)
            {
                problem(path + ".children", "does not have two nodes");
                return index;
            }
            std::array<std::size_t, 2> child_indices{};
            for (std::size_t child = 0; children != nullptr && child < 2; ++child)
            {
                child_indices[child] = read_node((*children)[child], std::format("{}.children[{}]", path, child), tree, docked, depth + 1);
            }
            LoadedNode& node = tree.nodes[index];
            node.split = children != nullptr;
            node.side_by_side = split == nullptr || *split == "LEFT_RIGHT";
            node.ratio = static_cast<float>(ratio);
            node.children = child_indices;
            return index;
        }
        check_members(*entry, path, {"windows", "selected", "central"});
        LoadedNode& node = tree.nodes[index];
        const JsonArray* windows = array(member(*entry, "windows", path), path + ".windows");
        for (std::size_t window = 0; windows != nullptr && window < windows->size(); ++window)
        {
            const std::string window_path = std::format("{}.windows[{}]", path, window);
            if (const std::optional<std::uint32_t> id = identifier(&(*windows)[window], window_path))
            {
                if (std::ranges::contains(docked, *id))
                {
                    problem(window_path, "is docked twice");
                }
                docked.push_back(*id);
                tree.nodes[index].windows.push_back(*id);
            }
        }
        if (const JsonValue* selected = optional_member(*entry, "selected"))
        {
            tree.nodes[index].selected = identifier(selected, path + ".selected").value_or(0);
            if (!std::ranges::contains(tree.nodes[index].windows, tree.nodes[index].selected))
            {
                problem(path + ".selected", "is not one of the node's windows");
            }
        }
        tree.nodes[index].central = boolean(optional_member(*entry, "central"), path + ".central").value_or(false);
        static_cast<void>(node);
        return index;
    }

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

    [[nodiscard]] auto number(const JsonValue* value,
                              std::string_view path) -> std::optional<double>
    {
        if (value == nullptr)
        {
            return std::nullopt;
        }
        if (const auto* result = std::get_if<double>(&value->value); result != nullptr && std::isfinite(*result))
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

    /// An identifier written "0x" and up to eight hexadecimal digits (not zero).
    [[nodiscard]] auto identifier(const JsonValue* value,
                                  std::string_view path) -> std::optional<std::uint32_t>
    {
        const std::string* written = text(value, path);
        if (written == nullptr)
        {
            return std::nullopt;
        }
        std::uint32_t id = 0;
        const bool prefixed = written->size() > 2 && written->size() <= 10 && (*written)[0] == '0' && ((*written)[1] == 'x' || (*written)[1] == 'X');
        const char* const end = written->data() + written->size();
        const auto [stop, error] = prefixed ? std::from_chars(written->data() + 2, end, id, 16) : std::from_chars_result{written->data(), std::errc::invalid_argument};
        if (!prefixed || error != std::errc{} || stop != end || id == 0)
        {
            problem(path, std::format("is \"{}\", not an identifier (\"0x\" and hexadecimal digits)", *written));
            return std::nullopt;
        }
        return id;
    }

    /// Two numbers [x, y].
    [[nodiscard]] auto vector(const JsonValue* value,
                              std::string_view path) -> std::optional<Vector2>
    {
        const JsonArray* pair = array(value, path);
        if (pair == nullptr)
        {
            return std::nullopt;
        }
        const auto* x = pair->size() == 2 ? std::get_if<double>(&(*pair)[0].value) : nullptr;
        const auto* y = pair->size() == 2 ? std::get_if<double>(&(*pair)[1].value) : nullptr;
        if (x == nullptr || y == nullptr || !std::isfinite(*x) || !std::isfinite(*y))
        {
            problem(path, "is not two numbers [x, y]");
            return std::nullopt;
        }
        return Vector2{.x = static_cast<float>(*x), .y = static_cast<float>(*y)};
    }

    std::vector<std::string>& m_problems;
};

/// The window of identifier @p id (made when it does not exist, with @p name until it is submitted).
auto
window_for(ContextState& state,
           std::uint32_t id,
           std::string_view name) -> std::uint32_t
{
    std::uint32_t window = state.find_window(id);
    if (window == ContextState::NO_WINDOW)
    {
        window = state.create_window(id, name, ContextState::NO_WINDOW);
    }
    return window;
}

/// Builds node @p loaded (of @p tree) into node @p node, its children under it.
void
build_node(ContextState& state,
           const LoadedTree& tree,
           std::size_t loaded,
           std::uint32_t node,
           std::uint32_t depth)
{
    const LoadedNode& source = tree.nodes[loaded];
    if (source.split && depth < 32)
    {
        const std::uint32_t first = state.create_dock_node(state.dock_nodes[node].dock_space, node);
        const std::uint32_t second = state.create_dock_node(state.dock_nodes[node].dock_space, node);
        DockNodeState& split = state.dock_nodes[node];
        split.children = {first, second};
        split.side_by_side = source.side_by_side;
        split.ratio = source.ratio;
        split.central = false;
        build_node(state, tree, source.children[0], first, depth + 1);
        build_node(state, tree, source.children[1], second, depth + 1);
        return;
    }
    state.dock_nodes[node].central = source.central;
    for (const std::uint32_t id : source.windows)
    {
        const std::uint32_t window = window_for(state, id, {});
        state.dock_nodes[node].windows.push_back(id);
        state.windows[window].dock_node = node;
    }
    state.dock_nodes[node].selected = source.selected != 0 ? source.selected : source.windows.empty() ? 0U : source.windows.front();
    state.dock_nodes[node].selection_changed = true;
}

} // namespace

auto
UiContext::save_layout() const -> std::string
{
    const ContextState& state = *m_state;
    std::string text = std::format("{{\n  \"format\": {},\n  \"version\": {},\n  \"windows\": [", json_string(LAYOUT_FORMAT), LAYOUT_FORMAT_VERSION);

    // Root windows the user places (not popups, tooltips, hosts, child regions, NO_SAVED_SETTINGS).
    bool first = true;
    for (const WindowState& window : state.windows)
    {
        if (window.child || window.popup || window.tooltip || window.dock_tree != NONE || has_any(window.flags, UiWindowFlag::NO_SAVED_SETTINGS))
        {
            continue;
        }
        text += std::format("{}\n    {{\"id\": {}, \"name\": {}, \"position\": [{}, {}], \"size\": [{}, {}], \"collapsed\": {}}}", first ? "" : ",",
                            json_identifier(window.id), json_string(window.name), window.position.x, window.position.y, std::max(window.size.x, 1.0F),
                            std::max(window.size.y, 1.0F), window.collapsed);
        first = false;
    }
    text += "\n  ],\n  \"dock_trees\": [";

    // Every tree: dock spaces' by their identifier, floating ones by their host's place.
    first = true;
    for (std::uint32_t node = 0; node < state.dock_nodes.size(); ++node)
    {
        const DockNodeState& root = state.dock_nodes[node];
        if (!root.alive || root.parent != NONE)
        {
            continue;
        }
        std::string place{};
        if (root.dock_space != 0)
        {
            place = std::format("\"dock_space\": {}", json_identifier(root.dock_space));
        }
        else if (root.host_window < state.windows.size())
        {
            const WindowState& host = state.windows[root.host_window];
            place = std::format("\"position\": [{}, {}], \"size\": [{}, {}]", host.position.x, host.position.y, std::max(host.size.x, 1.0F), std::max(host.size.y, 1.0F));
        }
        else
        {
            continue;
        }
        text += std::format("{}\n    {{{}, \"root\": {}}}", first ? "" : ",", place, node_json(state, node, 0));
        first = false;
    }
    text += "\n  ],\n  \"tables\": [";

    // Tables submitted at least once (or loaded), without NO_SAVED_SETTINGS.
    first = true;
    for (const TableState& table : state.tables)
    {
        if (table.columns.empty() || has_any(table.flags, TableFlag::NO_SAVED_SETTINGS))
        {
            continue;
        }
        text += std::format("{}\n    {{\"id\": {}, \"columns\": [", first ? "" : ",", json_identifier(table.id));
        for (std::size_t column = 0; column < table.columns.size(); ++column)
        {
            const TableColumnState& entry = table.columns[column];
            text += std::format("{}{{\"width\": {}, \"weight\": {}, \"order\": {}, \"visible\": {}, \"sort_order\": {}, \"sort_direction\": {}}}",
                                column == 0 ? "" : ", ", entry.width_request, entry.stretch_weight, entry.display_order, entry.user_enabled, entry.sort_order,
                                json_string(to_string(entry.sort_direction)));
        }
        text += "]}";
        first = false;
    }
    text += "\n  ]\n}\n";
    return text;
}

auto
UiContext::load_layout(std::string_view json) -> std::expected<void, std::string>
{
    ContextState& state = *m_state;
    if (state.frame_active)
    {
        throw InvalidUsageError({.backend = UiSubsystem::WIDGETS, .operation = "UiContext::load_layout", .problem = "called during a frame",
                                 .suggestion = "load layouts between end_frame() and begin_frame()"});
    }

    // The document, then every field checked (each problem with its path).
    DocumentBuilder builder;
    if (!json::parse(json::Text{json.data(), json.size()}, builder))
    {
        return std::unexpected(std::string{builder.error_message().empty() ? std::string_view{"malformed JSON"} : builder.error_message()});
    }
    std::vector<std::string> problems{};
    const LoadedLayout layout = LayoutReader{problems}.read(builder.root());
    if (!problems.empty())
    {
        std::string message = std::format("not a layout of format \"{}\" version {}:", LAYOUT_FORMAT, LAYOUT_FORMAT_VERSION);
        for (const std::string& problem : problems)
        {
            message += "\n  " + problem;
        }
        return std::unexpected(std::move(message));
    }

    // Applied at once: windows' places, the dock trees (replacing every tree), the tables' columns.
    for (const LoadedWindow& loaded : layout.windows)
    {
        WindowState& window = state.windows[window_for(state, loaded.id, loaded.name)];
        window.position = loaded.position;
        window.size = loaded.size;
        window.collapsed = loaded.collapsed;
        window.first_use = false;
    }
    state.clear_docking();
    for (const LoadedTree& tree : layout.trees)
    {
        if (tree.nodes.empty())
        {
            continue;
        }
        std::uint32_t root = NONE;
        if (tree.dock_space != 0)
        {
            root = state.dock_space_root(tree.dock_space);
        }
        else
        {
            // A floating tree's host window, where it was.
            root = state.create_dock_node(0, NONE);
            const std::string name = std::format("##dock_host_{:08X}", state.dock_nodes[root].id);
            WindowState& host = state.windows[window_for(state, ContextState::hash(0, name), name)];
            host.position = tree.position;
            host.size = tree.size;
            host.first_use = false;
            host.dock_tree = root;
            state.dock_nodes[root].host_window = state.find_window(ContextState::hash(0, name));
        }
        build_node(state, tree, 0, root, 0);
    }
    for (const LoadedTable& loaded : layout.tables)
    {
        std::uint32_t index = state.table_lookup.find(loaded.id).value_or(NONE);
        if (index == NONE)
        {
            index = static_cast<std::uint32_t>(state.tables.size());
            state.tables.emplace_back();
            state.tables.back().id = loaded.id;
            if (state.table_lookup.insert(loaded.id, index))
            {
                ++state.allocations;
            }
        }
        TableState& table = state.tables[index];
        table.columns.assign(loaded.columns.size(), TableColumnState{});
        table.display.assign(loaded.columns.size(), 0U);
        table.sort.clear();
        table.sort.reserve(loaded.columns.size());
        table.cell_backgrounds.clear();
        table.cell_backgrounds.reserve(loaded.columns.size());
        for (std::size_t column = 0; column < loaded.columns.size(); ++column)
        {
            const LoadedColumn& source = loaded.columns[column];
            TableColumnState& entry = table.columns[column];
            entry.initialized = true;
            entry.width_request = source.width;
            entry.stretch_weight = source.weight;
            entry.display_order = source.order;
            entry.user_enabled = source.visible;
            entry.sort_order = source.sort_direction == SortDirection::NONE ? -1 : source.sort_order;
            entry.sort_direction = entry.sort_order < 0 ? SortDirection::NONE : source.sort_direction;
            table.display[source.order] = static_cast<std::uint32_t>(column);
        }
        table.sort_initialized = true;
        table.sort_changed = true;
    }
    return {};
}

} // namespace gfx::ui
