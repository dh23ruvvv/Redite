#include "Cache.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

Cache::Cache(size_t capacity,
             std::unique_ptr<EvictionPolicy> policy,
             std::unique_ptr<PersistenceManager> persistence)
    : capacity_(capacity),
      policy_(std::move(policy)),
      persistence_(std::move(persistence)) {
    // Bug 4 fix: capacity must be at least 1, otherwise evictionCandidate()
    // would be called on an empty policy (undefined behavior on std::list).
    if (capacity_ == 0) {
        throw std::invalid_argument("Cache capacity must be at least 1");
    }
}

// ---------------------------------------------------------------------------
// get — look up a key.
//
// Lazy expiration happens here: if the key exists but its TTL has elapsed,
// we remove it and return nullopt, as if it was never there.
// ---------------------------------------------------------------------------
std::optional<std::string> Cache::get(const std::string& key) {
    auto it = store_.find(key);
    if (it == store_.end()) {
        return std::nullopt;  // Key not found.
    }

    // Lazy expiration check.
    if (it->second.isExpired()) {
        std::cout << "  [Cache] Key \"" << key << "\" has expired — removing.\n";
        removeInternal(key);
        return std::nullopt;
    }

    // Key is alive — tell the eviction policy it was just used.
    policy_->recordAccess(key);
    return it->second.value;
}

// ---------------------------------------------------------------------------
// put — insert or overwrite a key.
//
// If this is a brand-new key and we're at capacity, ask the eviction policy
// which key to evict first.  Overwriting an existing key does NOT count as a
// new insertion for capacity purposes.
// ---------------------------------------------------------------------------
void Cache::put(const std::string& key,
                const std::string& value,
                std::optional<int> ttlSeconds) {
    auto now = std::chrono::steady_clock::now();

    // Check if the key already exists (overwrite, not a new insertion).
    auto it = store_.find(key);
    if (it != store_.end()) {
        // Overwrite the existing entry.
        it->second.value = value;
        it->second.insertedAt = now;
        it->second.expiresAt = ttlSeconds.has_value()
            ? std::optional(now + std::chrono::seconds(ttlSeconds.value()))
            : std::nullopt;
        policy_->recordAccess(key);
        return;
    }

    // New key — evict if we're at capacity.
    if (store_.size() >= capacity_) {
        std::string victim = policy_->evictionCandidate();
        std::cout << "  [Cache] At capacity (" << capacity_
                  << ") — evicting key \"" << victim << "\"\n";
        removeInternal(victim);
    }

    // Build the new entry.
    CacheEntry entry;
    entry.key = key;
    entry.value = value;
    entry.insertedAt = now;
    if (ttlSeconds.has_value()) {
        entry.expiresAt = now + std::chrono::seconds(ttlSeconds.value());
    }

    store_[key] = std::move(entry);
    policy_->recordInsert(key);
}

// ---------------------------------------------------------------------------
// del — explicitly delete a key.
// ---------------------------------------------------------------------------
bool Cache::del(const std::string& key) {
    if (store_.find(key) == store_.end()) {
        return false;
    }
    removeInternal(key);
    return true;
}

size_t Cache::size() const {
    return store_.size();
}

// ---------------------------------------------------------------------------
// Snapshot helpers — delegate to PersistenceManager.
// ---------------------------------------------------------------------------
void Cache::saveSnapshot() const {
    persistence_->snapshot(store_);
}

// ---------------------------------------------------------------------------
// loadSnapshot — restore cache state from disk.
//
// Bug 1 fix: we insert entries directly into the store with the expiresAt
// that PersistenceManager already computed, instead of re-deriving a TTL
// integer (which would truncate sub-second remainders a second time,
// silently eroding TTLs by up to 2 seconds per save/load cycle).
// ---------------------------------------------------------------------------
void Cache::loadSnapshot() {
    auto entries = persistence_->loadSnapshot();
    for (auto& entry : entries) {
        // Skip entries that have already expired between load and now.
        if (entry.expiresAt.has_value() && entry.isExpired()) {
            continue;
        }

        // If at capacity, evict the least-recently-used loaded entry.
        if (store_.size() >= capacity_) {
            std::string victim = policy_->evictionCandidate();
            removeInternal(victim);
        }

        // Insert directly into the store, preserving the exact expiresAt
        // time_point that PersistenceManager computed — no second truncation.
        std::string k = entry.key;
        store_[k] = std::move(entry);
        policy_->recordInsert(k);
    }
}

// ---------------------------------------------------------------------------
// removeInternal — erase a key from both the hash map and the policy.
// ---------------------------------------------------------------------------
void Cache::removeInternal(const std::string& key) {
    store_.erase(key);
    policy_->remove(key);
}
