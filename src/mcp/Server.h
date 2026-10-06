#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lectern::mcp {

using Json = nlohmann::json;
enum class Access { Denied, Read, Edit, Full };

struct Session {
    std::string id;
    std::string clientName;
    std::string protocolVersion;
    Access access = Access::Denied;
    bool initialized = false;
    bool ready = false;
};

struct Context {
    const Session& session;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::function<void(double, std::optional<double>, std::string_view)> progress;
};

struct Tool {
    std::string name;
    std::string description;
    Json inputSchema;
    Access access = Access::Read;
    bool destructive = false;
    bool idempotent = false;
    std::function<Json(const Json&, const Context&)> call;
};

// The registry and protocol layer are Qt-free. A transport owns each Session
// and grants access only after the local user approves that connection.
class Server {
public:
    using Notify = std::function<void(const Json&)>;
    static constexpr std::size_t maxMessageBytes = 1024 * 1024;
    static constexpr std::string_view protocolVersion = "2025-11-25";
    /// The `initialize` handshake revisions this server speaks (a client's choice is echoed).
    static constexpr std::array<std::string_view, 4> handshakeVersions{"2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25"};

    void addTool(Tool tool);
    void addResource(std::string uri, std::string name, std::string description,
                     std::function<Json(const Context&)> read);
    void addPrompt(std::string name, std::string description, std::string text);

    [[nodiscard]] std::optional<Json> handle(std::string_view line, Session& session, const Notify& notify = {});
    [[nodiscard]] std::optional<Json> handle(const Json& message, Session& session, const Notify& notify = {});
    [[nodiscard]] Json tools() const;
    [[nodiscard]] const Tool* tool(std::string_view name) const;
    // Validates the closed schema vocabulary used by the registry: object,
    // array, string, boolean, integer/number, enum, bounds and required keys.
    [[nodiscard]] static std::optional<std::string> validate(const Json& value, const Json& schema,
                                                           std::string path = "arguments");
    [[nodiscard]] static Json result(const Json& data);
    [[nodiscard]] static Json failure(std::string_view message);
    [[nodiscard]] static Json error(const Json& id, int code, std::string_view message);

private:
    struct Resource { Json description; std::function<Json(const Context&)> read; };
    struct Prompt { Json description; std::string text; };
    std::map<std::string, Tool, std::less<>> tools_;
    std::map<std::string, Resource, std::less<>> resources_;
    std::map<std::string, Prompt, std::less<>> prompts_;
    std::mutex activeMutex_;
    std::map<std::pair<std::string, std::string>, std::shared_ptr<std::atomic_bool>> active_;
};

} // namespace lectern::mcp
