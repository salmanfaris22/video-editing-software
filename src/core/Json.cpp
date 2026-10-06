#include "core/Json.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace lectern::json {

Result<Json> parse(std::string_view text, std::string_view what) {
    try {
        return Json::parse(text.begin(), text.end());
    } catch (const nlohmann::json::parse_error& e) {
        return fail(ErrorCode::ParseError, std::string(what) + ": " + e.what(), e.byte);
    }
}

std::string dump(const Json& value, bool pretty) {
    // `replace` keeps invalid UTF-8 from throwing during serialization.
    return value.dump(pretty ? 2 : -1, ' ', false, nlohmann::json::error_handler_t::replace);
}

Json toJson(Time t) { return t.ticks(); }

Result<Time> timeFrom(const Json& value, std::string_view path) {
    if (!value.is_number_integer()) return fail(ErrorCode::ParseError, std::string(path) + ": expected integer ticks");
    return Time::fromTicks(value.get<std::int64_t>());
}

Json toJson(Rational r) { return Json{{"num", r.num()}, {"den", r.den()}}; }

Result<Rational> rationalFrom(const Json& value, std::string_view path) {
    if (!value.is_object() || !value.contains("num") || !value.contains("den") || !value["num"].is_number_integer() ||
        !value["den"].is_number_integer()) {
        return fail(ErrorCode::ParseError, std::string(path) + ": expected {num, den}");
    }
    const auto den = value["den"].get<std::int64_t>();
    if (den == 0) return fail(ErrorCode::ParseError, std::string(path) + ": zero denominator");
    return Rational(value["num"].get<std::int64_t>(), den);
}

Json toJson(const Uuid& id) { return id.toString(); }

Result<Uuid> uuidFrom(const Json& value, std::string_view path) {
    if (!value.is_string()) return fail(ErrorCode::ParseError, std::string(path) + ": expected uuid string");
    auto parsed = Uuid::parse(value.get_ref<const std::string&>());
    if (!parsed) return fail(ErrorCode::ParseError, std::string(path) + ": malformed uuid");
    return *parsed;
}

std::string utcNowIso8601() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

}  // namespace lectern::json
