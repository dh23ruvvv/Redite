#ifndef CACHE_ENTRY_H
#define CACHE_ENTRY_H

#include <chrono>
#include <optional>
#include <string>

// CacheEntry — one stored item in the cache.
//
// Each entry records the key, value, when it was inserted, and an optional
// expiration time.  If expiresAt is std::nullopt the key never expires.
struct CacheEntry {
    std::string key;
    std::string value;

    // Wall-clock time when this entry was inserted (or last overwritten).
    std::chrono::steady_clock::time_point insertedAt;

    // If set, the entry is considered expired once steady_clock::now() passes
    // this point.  std::nullopt means "lives forever."
    std::optional<std::chrono::steady_clock::time_point> expiresAt;

    // Returns true when the entry has a TTL and that TTL has elapsed.
    bool isExpired() const {
        if (!expiresAt.has_value()) {
            return false;  // No expiration set — never expires.
        }
        return std::chrono::steady_clock::now() >= expiresAt.value();
    }
};

#endif // CACHE_ENTRY_H
