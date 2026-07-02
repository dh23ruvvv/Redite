#ifndef CACHE_H
#define CACHE_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "CacheEntry.h"
#include "EvictionPolicy.h"
#include "PersistenceManager.h"

// ---------------------------------------------------------------------------
// Cache — the main class you interact with.
//
// Owns:
//   - A hash map of key → CacheEntry for O(1) reads and writes.
//   - An EvictionPolicy (LRUPolicy) that decides which key to evict when
//     the cache is at capacity.
//   - A PersistenceManager for saving/loading snapshots to disk.
//
// Behavior:
//   - get() performs lazy expiration: if a key's TTL has elapsed, it is
//     silently removed and treated as a miss.
//   - put() evicts via the policy if the cache is already at capacity and
//     we're inserting a truly new key (overwriting an existing key does NOT
//     trigger eviction).
// ---------------------------------------------------------------------------
class Cache {
public:
    // capacity: maximum number of entries before eviction kicks in.
    // policy:   ownership of the eviction policy (e.g. std::make_unique<LRUPolicy>()).
    // persistence: ownership of the persistence manager.
    Cache(size_t capacity,
          std::unique_ptr<EvictionPolicy> policy,
          std::unique_ptr<PersistenceManager> persistence);

    // Retrieve the value for `key`, or std::nullopt if missing / expired.
    std::optional<std::string> get(const std::string& key);

    // Insert or overwrite `key`.  If ttlSeconds is provided, the key expires
    // that many seconds from now.
    void put(const std::string& key,
             const std::string& value,
             std::optional<int> ttlSeconds = std::nullopt);

    // Remove `key` from the cache.  Returns true if the key existed.
    bool del(const std::string& key);

    // Number of entries currently stored (includes not-yet-lazily-expired ones).
    size_t size() const;

    // Read-only access to every entry in the store.  Used by application
    // layers (e.g. Splitwise) that need to scan all keys by prefix.
    const std::unordered_map<std::string, CacheEntry>& getAll() const;

    // Persist current state to disk.
    void saveSnapshot() const;

    // Restore state from disk (called once at startup).
    void loadSnapshot();

private:
    size_t capacity_;
    std::unordered_map<std::string, CacheEntry> store_;
    std::unique_ptr<EvictionPolicy> policy_;
    std::unique_ptr<PersistenceManager> persistence_;

    // Internal helper: remove a key from both the store and the policy.
    void removeInternal(const std::string& key);
};

#endif // CACHE_H
