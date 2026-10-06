#include "mcp/Server.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

using namespace lectern::mcp;
namespace {
Json request(std::string method, Json params = Json::object(), Json id = 1) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
void initialize(Server& server, Session& s) {
    const auto result = server.handle(request("initialize", {{"protocolVersion", "2025-11-25"}, {"capabilities", Json::object()},
        {"clientInfo", {{"name", "test"}, {"version", "1"}}}}), s);
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->contains("result")) << result->dump();
    EXPECT_FALSE(server.handle(Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, s));
}
Tool counter(int& count) {
    return {"increment", "Increment a test counter", {{"type", "object"}, {"properties", {{"amount", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5}}}}},
            {"required", {"amount"}}, {"additionalProperties", false}}, Access::Edit, false, false,
            [&count](const Json& a, const Context&) { count += a["amount"].get<int>(); return Server::result(Json{{"count", count}}); }};
}
} // namespace

TEST(McpServer, LifecycleAndProtocolNegotiation) {
    Server server;
    Session s{.id = "s"};
    EXPECT_EQ((*server.handle(request("tools/list"), s))["error"]["code"], -32000);
    EXPECT_EQ((*server.handle(request("initialize"), s))["error"]["code"], -32602);
    initialize(server, s);
    EXPECT_EQ(s.protocolVersion, "2025-11-25");
    EXPECT_EQ(s.clientName, "test");
    EXPECT_EQ(s.access, Access::Denied);
    EXPECT_TRUE((*server.handle(request("tools/list"), s))["result"]["tools"].is_array());
    EXPECT_EQ((*server.handle(request("initialize"), s))["error"]["code"], -32600);
    EXPECT_EQ((*server.handle(request("missing"), s))["error"]["code"], -32601);
    EXPECT_EQ((*server.handle(request("ping"), s))["result"], Json::object());
}

TEST(McpServer, EchoesEveryHandshakeRevisionAndAnswersProbesBeforeIt) {
    for (const char* version : {"2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25"}) {
        Server server;
        Session s{.id = "s"};
        const auto reply = server.handle(request("initialize", {{"protocolVersion", version}, {"capabilities", Json::object()},
                                                                {"clientInfo", {{"name", "c"}, {"version", "1"}}}}), s);
        EXPECT_EQ((*reply)["result"]["protocolVersion"], version);
    }
    Server server;
    Session s{.id = "s"};
    // A future revision gets our newest one back; the client decides whether to go on.
    const auto reply = server.handle(request("initialize", {{"protocolVersion", "2031-01-01"}, {"capabilities", Json::object()},
                                                            {"clientInfo", {{"name", "c"}, {"version", "1"}}}}), s);
    EXPECT_EQ((*reply)["result"]["protocolVersion"], std::string(Server::protocolVersion));
    // The stateless-era probe before the handshake: "method not found", so the client falls back.
    Session fresh{.id = "f"};
    EXPECT_EQ((*server.handle(request("server/discover", {{"protocolVersion", "2026-07-28"}}), fresh))["error"]["code"], -32601);
    EXPECT_EQ((*server.handle(request("tools/list"), fresh))["error"]["code"], -32000);
}

TEST(McpServer, RejectsMalformedMessagesBatchesAndDeepNesting) {
    Server server; Session s;
    EXPECT_EQ((*server.handle(std::string_view("{"), s))["error"]["code"], -32700);
    for (const auto& bad : {Json::array(), Json(nullptr), Json{{"jsonrpc", "1.0"}, {"method", "ping"}},
                           Json{{"jsonrpc", "2.0"}, {"method", "ping"}, {"id", nullptr}}})
        EXPECT_EQ((*server.handle(bad, s))["error"]["code"], -32600);
    const std::string huge(Server::maxMessageBytes + 1, ' ');
    EXPECT_EQ((*server.handle(std::string_view(huge), s))["error"]["code"], -32600);
    const std::string nested = std::string(100, '[') + "0" + std::string(100, ']');
    EXPECT_EQ((*server.handle(std::string_view(nested), s))["error"]["code"], -32700);
    EXPECT_FALSE(server.handle(Json{{"jsonrpc", "2.0"}, {"method", "notifications/unknown"}}, s));
}

TEST(McpServer, SchemaAndPermissionsRejectWithoutCallingTool) {
    int count = 0;
    Server server; server.addTool(counter(count)); Session s{.id = "s"}; initialize(server, s);
    const auto call = [&](Json args) { return *server.handle(request("tools/call", {{"name", "increment"}, {"arguments", args}}), s); };
    EXPECT_TRUE(call({{"amount", 1}})["result"]["isError"].get<bool>());
    s.access = Access::Read;
    EXPECT_TRUE(call({{"amount", 1}})["result"]["isError"].get<bool>());
    s.access = Access::Edit;
    for (const auto& bad : {Json::object(), Json{{"amount", "1"}}, Json{{"amount", 0}}, Json{{"amount", 6}}, Json{{"amount", 1}, {"extra", true}}})
        EXPECT_EQ(call(bad)["error"]["code"], -32602);
    EXPECT_EQ(count, 0);
    const auto good = call({{"amount", 3}});
    EXPECT_EQ(count, 3);
    EXPECT_EQ(good["result"]["structuredContent"]["count"], 3);
    EXPECT_FALSE(server.tools()[0]["annotations"]["readOnlyHint"].get<bool>());
}

TEST(McpServer, ResourcesRequireConsentAndPromptsAreDiscoverable) {
    Server server; Session s{.id = "s"}; initialize(server, s);
    server.addResource("lectern://project/current", "Project", "Summary", [](const Context&) { return Json{{"title", "Example"}}; });
    server.addPrompt("review", "Review the timeline", "Read the timeline and explain the proposed edit.");
    EXPECT_EQ((*server.handle(request("resources/list"), s))["result"]["resources"].size(), 1u);
    const auto read = request("resources/read", {{"uri", "lectern://project/current"}});
    EXPECT_EQ((*server.handle(read, s))["error"]["code"], -32001);
    s.access = Access::Read;
    EXPECT_EQ((*server.handle(read, s))["result"]["contents"][0]["text"], "{\"title\":\"Example\"}");
    EXPECT_EQ((*server.handle(request("prompts/get", {{"name", "review"}}), s))["result"]["messages"][0]["role"], "user");
    EXPECT_EQ((*server.handle(request("resources/read", {{"uri", "file:///etc/passwd"}}), s))["error"]["code"], -32002);
}

TEST(McpServer, ProgressAndCancellationAreScopedToSessionAndRequest) {
    Server server;
    Session s{.id = "one", .access = Access::Read}; initialize(server, s);
    Session other{.id = "two", .access = Access::Read}; initialize(server, other);
    std::atomic_bool started = false;
    bool cancelled = false;
    server.addTool({"work", "Cancellable work", {{"type", "object"}}, Access::Read, false, false,
        [&](const Json&, const Context& c) {
            c.progress(1, 2, "Started");
            c.progress(1, 2, "Duplicate suppressed");
            started.store(true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!c.cancelled->load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
            cancelled = c.cancelled->load();
            return Server::result(Json{{"cancelled", cancelled}});
        }});
    std::vector<Json> notifications;
    std::thread worker([&] { (void)server.handle(request("tools/call", {{"name", "work"}, {"_meta", {{"progressToken", "p"}}}}, 9), s,
                                                [&](const Json& n) { notifications.push_back(n); }); });
    while (!started.load()) std::this_thread::yield();
    const Json cancel{{"jsonrpc", "2.0"}, {"method", "notifications/cancelled"}, {"params", {{"requestId", 9}}}};
    EXPECT_FALSE(server.handle(cancel, other));
    EXPECT_FALSE(server.handle(cancel, s));
    worker.join();
    EXPECT_TRUE(cancelled);
    ASSERT_EQ(notifications.size(), 1u);
    EXPECT_EQ(notifications[0]["params"]["progressToken"], "p");
}
