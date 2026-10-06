#include "mcp/Server.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lectern::mcp {

Json Server::error(const Json& id, int code, std::string_view message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

Json Server::result(const Json& data) {
    return {{"content", Json::array({{{"type", "text"}, {"text", data.dump()}}})},
            {"structuredContent", data.is_object() ? data : Json{{"value", data}}}, {"isError", false}};
}

Json Server::failure(std::string_view message) {
    return {{"content", Json::array({{{"type", "text"}, {"text", message}}})}, {"isError", true}};
}

void Server::addTool(Tool value) {
    if (value.name.empty() || !value.call || !value.inputSchema.is_object() || tools_.contains(value.name))
        throw std::invalid_argument("Invalid or duplicate MCP tool");
    tools_.emplace(value.name, std::move(value));
}

void Server::addResource(std::string uri, std::string name, std::string description,
                         std::function<Json(const Context&)> read) {
    Json info{{"uri", uri}, {"name", name}, {"description", description}, {"mimeType", "application/json"}};
    resources_.emplace(std::move(uri), Resource{std::move(info), std::move(read)});
}

void Server::addPrompt(std::string name, std::string description, std::string text) {
    Json info{{"name", name}, {"description", description}};
    prompts_.emplace(std::move(name), Prompt{std::move(info), std::move(text)});
}

const Tool* Server::tool(std::string_view name) const {
    const auto it = tools_.find(name);
    return it == tools_.end() ? nullptr : &it->second;
}

Json Server::tools() const {
    Json list = Json::array();
    for (const auto& [name, t] : tools_) {
        list.push_back({{"name", name}, {"description", t.description}, {"inputSchema", t.inputSchema},
                        {"annotations", {{"readOnlyHint", t.access == Access::Read},
                                         {"destructiveHint", t.destructive}, {"idempotentHint", t.idempotent},
                                         {"openWorldHint", false}}}});
    }
    return list;
}

std::optional<std::string> Server::validate(const Json& v, const Json& s, std::string path) {
    const auto type = s.value("type", std::string{});
    const bool valid = (type == "object" && v.is_object()) || (type == "array" && v.is_array()) ||
                       (type == "string" && v.is_string()) || (type == "boolean" && v.is_boolean()) ||
                       (type == "number" && v.is_number()) || (type == "integer" && v.is_number_integer());
    if (!valid) return path + " must be " + type;
    if (s.contains("enum") && std::find(s["enum"].begin(), s["enum"].end(), v) == s["enum"].end())
        return path + " is not an allowed value";
    if (v.is_number()) {
        const auto n = v.get<double>();
        if (!std::isfinite(n) || (s.contains("minimum") && n < s["minimum"].get<double>()) ||
            (s.contains("maximum") && n > s["maximum"].get<double>())) return path + " is out of range";
    }
    if (v.is_string()) {
        const auto size = v.get_ref<const std::string&>().size();
        if (size < s.value("minLength", std::size_t{0}) || size > s.value("maxLength", std::size_t{65536}))
            return path + " has an invalid length";
    }
    if (v.is_array()) {
        if (v.size() < s.value("minItems", std::size_t{0}) || v.size() > s.value("maxItems", std::size_t{1000}))
            return path + " has an invalid item count";
        if (s.contains("items")) for (std::size_t i = 0; i < v.size(); ++i)
            if (auto issue = validate(v[i], s["items"], path + "[" + std::to_string(i) + "]")) return issue;
    }
    if (v.is_object()) {
        for (const auto& key : s.value("required", Json::array()))
            if (!v.contains(key.get<std::string>())) return path + "." + key.get<std::string>() + " is required";
        const auto props = s.value("properties", Json::object());
        for (const auto& [key, value] : v.items()) {
            if (!props.contains(key)) {
                if (!s.value("additionalProperties", false)) return path + "." + key + " is unknown";
            } else if (auto issue = validate(value, props[key], path + "." + key)) return issue;
        }
    }
    return std::nullopt;
}

std::optional<Json> Server::handle(std::string_view line, Session& session, const Notify& notify) {
    if (line.size() > maxMessageBytes) return error(nullptr, -32600, "Message too large");
    try {
        int depth = 0;
        const auto value = Json::parse(line, [&depth](int level, Json::parse_event_t, Json&) {
            depth = std::max(depth, level);
            if (depth > 64) throw std::invalid_argument("JSON nesting limit");
            return true;
        });
        return handle(value, session, notify);
    } catch (const std::exception&) {
        return error(nullptr, -32700, "Invalid JSON");
    }
}

std::optional<Json> Server::handle(const Json& m, Session& session, const Notify& notify) {
    if (!m.is_object() || !m.contains("jsonrpc") || m["jsonrpc"] != "2.0" ||
        !m.contains("method") || !m["method"].is_string() ||
        (m.contains("id") && !m["id"].is_string() && !m["id"].is_number_integer()))
        return error(nullptr, -32600, "Expected one JSON-RPC 2.0 message (batches are unsupported)");
    const bool request = m.contains("id");
    const Json id = request ? m["id"] : Json(nullptr);
    const std::string method = m["method"];
    if (m.contains("params") && !m["params"].is_object())
        return request ? std::optional(error(id, -32602, "params must be an object")) : std::nullopt;
    const auto p = m.value("params", Json::object());
    if (!request) {
        if (method == "notifications/initialized" && session.initialized) session.ready = true;
        if (method == "notifications/cancelled" && p.contains("requestId")) {
            std::lock_guard lock(activeMutex_);
            if (const auto it = active_.find({session.id, p["requestId"].dump()}); it != active_.end())
                it->second->store(true);
        }
        return std::nullopt;
    }
    const auto reply = [&id](Json value) -> std::optional<Json> {
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(value)}};
    };
    if (method == "ping") return reply(Json::object());
    if (method == "initialize") {
        if (session.initialized) return error(id, -32600, "Session already initialized");
        if (!p.contains("protocolVersion") || !p["protocolVersion"].is_string() ||
            !p.contains("capabilities") || !p["capabilities"].is_object() ||
            !p.contains("clientInfo") || !p["clientInfo"].is_object() ||
            !p["clientInfo"].contains("name") || !p["clientInfo"]["name"].is_string() ||
            !p["clientInfo"].contains("version") || !p["clientInfo"]["version"].is_string())
            return error(id, -32602, "initialize requires protocolVersion, capabilities and clientInfo");
        session.clientName = p["clientInfo"]["name"].get<std::string>().substr(0, 128);
        const std::string version = p["protocolVersion"];
        session.protocolVersion = (version == "2025-03-26" || version == "2025-06-18") ? version : std::string(protocolVersion);
        session.initialized = true;
        return reply({{"protocolVersion", session.protocolVersion},
                      {"capabilities", {{"tools", Json::object()}, {"resources", Json::object()}, {"prompts", Json::object()}}},
                      {"serverInfo", {{"name", "lectern"}, {"version", "0.1.0"}}},
                      {"instructions", "Approve this connection in Lectern's AI assistants panel before accessing a project. Edits use the app's undo history."}});
    }
    if (!session.ready) return error(id, -32000, "Initialize the session first");
    if (method == "tools/list") return reply({{"tools", tools()}});
    if (method == "resources/list") {
        Json list = Json::array();
        for (const auto& [uri, r] : resources_) list.push_back(r.description);
        return reply({{"resources", list}});
    }
    if (method == "resources/templates/list") return reply({{"resourceTemplates", Json::array()}});
    if (method == "prompts/list") {
        Json list = Json::array();
        for (const auto& [name, prompt] : prompts_) list.push_back(prompt.description);
        return reply({{"prompts", list}});
    }
    if (method == "prompts/get") {
        if (!p.contains("name") || !p["name"].is_string()) return error(id, -32602, "Prompt name is required");
        const auto it = prompts_.find(p["name"].get<std::string>());
        if (it == prompts_.end()) return error(id, -32602, "Unknown prompt");
        return reply({{"description", it->second.description["description"]},
                      {"messages", Json::array({{{"role", "user"}, {"content", {{"type", "text"}, {"text", it->second.text}}}}})}});
    }
    if (method != "tools/call" && method != "resources/read") return error(id, -32601, "Method not found");
    auto flag = std::make_shared<std::atomic_bool>(false);
    const auto key = std::make_pair(session.id, id.dump());
    {
        std::lock_guard lock(activeMutex_);
        if (!active_.emplace(key, flag).second) return error(id, -32600, "Duplicate active request id");
    }
    struct Cleanup {
        Server& server;
        decltype(key)& key;
        ~Cleanup() { std::lock_guard lock(server.activeMutex_); server.active_.erase(key); }
    } cleanup{*this, key};
    double previous = -1;
    const auto token = p.contains("_meta") && p["_meta"].is_object() ? p["_meta"].value("progressToken", Json()) : Json();
    Context context{session, flag, [&](double progress, std::optional<double> total, std::string_view message) {
        if (!notify || (!token.is_string() && !token.is_number()) || !std::isfinite(progress) || progress <= previous) return;
        previous = progress;
        Json params{{"progressToken", token}, {"progress", progress}, {"message", message}};
        if (total && std::isfinite(*total)) params["total"] = *total;
        notify({{"jsonrpc", "2.0"}, {"method", "notifications/progress"}, {"params", params}});
    }};
    try {
        if (method == "resources/read") {
            if (session.access < Access::Read) return error(id, -32001, "Approve this client in Lectern");
            if (!p.contains("uri") || !p["uri"].is_string()) return error(id, -32602, "Resource URI is required");
            const auto it = resources_.find(p["uri"].get<std::string>());
            if (it == resources_.end()) return error(id, -32002, "Resource not found");
            return reply({{"contents", Json::array({{{"uri", p["uri"]}, {"mimeType", "application/json"},
                                                     {"text", it->second.read(context).dump()}}})}});
        }
        if (!p.contains("name") || !p["name"].is_string()) return error(id, -32602, "Tool name is required");
        const auto* t = tool(p["name"].get<std::string>());
        if (!t) return error(id, -32602, "Unknown tool");
        const auto args = p.value("arguments", Json::object());
        if (auto issue = validate(args, t->inputSchema)) return error(id, -32602, *issue);
        if (session.access < t->access) return reply(failure("This client needs approval or a higher access level in Lectern."));
        return reply(t->call(args, context));
    } catch (const std::exception& ex) {
        return method == "tools/call" ? reply(failure(ex.what())) : std::optional(error(id, -32603, ex.what()));
    }
}

} // namespace lectern::mcp
