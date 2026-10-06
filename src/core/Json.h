#pragma once

#include "core/Error.h"
#include "core/Rational.h"
#include "core/Time.h"
#include "core/Uuid.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace lectern::json {

using Json = nlohmann::json;

/// Parses text into JSON without throwing.
Result<Json> parse(std::string_view text, std::string_view what = "json");
/// Serializes with stable formatting (2-space indent, keys in insertion order).
std::string dump(const Json& value, bool pretty = true);

/// Reads a required field and converts it; error messages carry the JSON path.
template <class T>
Result<T> require(const Json& obj, std::string_view key, std::string_view path) {
    const std::string fullPath = std::string(path) + "." + std::string(key);
    if (!obj.is_object()) return fail(ErrorCode::ParseError, std::string(path) + ": expected object");
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return fail(ErrorCode::ParseError, fullPath + ": missing");
    try {
        return it->template get<T>();
    } catch (const nlohmann::json::exception& e) {
        return fail(ErrorCode::ParseError, fullPath + ": " + e.what());
    }
}

/// Reads an optional field; returns `fallback` when absent or null.
template <class T>
Result<T> optional(const Json& obj, std::string_view key, std::string_view path, T fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return fallback;
    try {
        return it->template get<T>();
    } catch (const nlohmann::json::exception& e) {
        return fail(ErrorCode::ParseError, std::string(path) + "." + std::string(key) + ": " + e.what());
    }
}

// Value encodings shared by every on-disk format (docs/PROJECT_FORMAT.md §2).
Json toJson(Time t);
Result<Time> timeFrom(const Json& value, std::string_view path);
Json toJson(Rational r);
Result<Rational> rationalFrom(const Json& value, std::string_view path);
Json toJson(const Uuid& id);
Result<Uuid> uuidFrom(const Json& value, std::string_view path);

/// UTC timestamp "2026-10-04T12:30:05Z".
std::string utcNowIso8601();

}  // namespace lectern::json
