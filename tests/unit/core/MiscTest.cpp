#include "core/Error.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Uuid.h"

#include <gtest/gtest.h>

#include <set>

using namespace lectern;

TEST(Uuid, V7IsTimeOrderedUniqueAndRoundTrips) {
    std::set<Uuid> seen;
    Uuid prev = Uuid::generateV7();
    for (int i = 0; i < 5000; ++i) {
        const Uuid u = Uuid::generateV7();
        EXPECT_LT(prev, u);
        EXPECT_TRUE(seen.insert(u).second);
        prev = u;
    }
    const std::string s = prev.toString();
    EXPECT_EQ(s.size(), 36u);
    EXPECT_EQ(s[14], '7');  // version nibble
    EXPECT_EQ(Uuid::parse(s), prev);
    EXPECT_FALSE(Uuid::parse("not-a-uuid").has_value());
}

TEST(Error, ContextAndFormatting) {
    Error e = Error(ErrorCode::OutOfSpace, "write", 28).withContext("track screen");
    EXPECT_EQ(e.message(), "track screen: write");
    EXPECT_EQ(e.toString(), "[out-of-space] track screen: write (native 28)");
    EXPECT_EQ(errorCodeFromErrno(28 /* ENOSPC on POSIX */), ErrorCode::OutOfSpace);
}

namespace {
Status failing() { return fail(ErrorCode::NotFound, "nope"); }
Status propagates() {
    LEC_TRY(failing());
    return ok();
}
}  // namespace

TEST(Error, TryMacroPropagates) {
    auto st = propagates();
    ASSERT_FALSE(st);
    EXPECT_EQ(st.error().code(), ErrorCode::NotFound);
}

TEST(Json, TimeAndRationalEncodings) {
    EXPECT_EQ(json::toJson(Time::fromSeconds(1)).get<std::int64_t>(), Time::kTicksPerSecond);
    EXPECT_EQ(json::timeFrom(json::Json(42), "t").value(), Time::fromTicks(42));
    EXPECT_FALSE(json::timeFrom(json::Json(1.5), "t"));
    EXPECT_EQ(json::rationalFrom(json::toJson(Rational(30000, 1001)), "r").value(), Rational(30000, 1001));
    EXPECT_FALSE(json::rationalFrom(json::Json{{"num", 1}, {"den", 0}}, "r"));
    auto missing = json::require<int>(json::Json::object(), "x", "root");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().message(), "root.x: missing");
    EXPECT_FALSE(json::parse("{bad", "doc"));
}

TEST(Log, AsyncLoggerDeliversToSinks) {
    struct Capture : ILogSink {
        std::vector<std::string> lines;
        void write(const LogRecord& r) override { lines.push_back(r.category + ":" + r.message); }
    };
    auto sink = std::make_shared<Capture>();
    Logger::instance().addSink(sink);
    Logger::instance().setLevel(LogLevel::Debug);
    LEC_INFO("test", "hello {}", 42);
    LEC_TRACE("test", "filtered {}", 1);
    Logger::instance().flush();
    Logger::instance().clearSinks();
    ASSERT_FALSE(sink->lines.empty());
    EXPECT_EQ(sink->lines.back(), "test:hello 42");
}
