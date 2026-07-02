#ifndef EVICTION_POLICY_H
#define EVICTION_POLICY_H

#include <list>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// EvictionPolicy — abstract interface for cache eviction strategies.
//
// Any concrete policy must be able to:
//   1. Track insertions and accesses so it can decide ordering.
//   2. Name the next key to evict when the cache is full.
//   3. Remove a key from its internal bookkeeping (e.g. on DEL or expiry).
// ---------------------------------------------------------------------------
class EvictionPolicy {
public:
    virtual ~EvictionPolicy() = default;

    // Called every time an existing key is read or written.
    virtual void recordAccess(const std::string& key) = 0;

    // Called when a brand-new key is inserted into the cache.
    virtual void recordInsert(const std::string& key) = 0;

    // Returns the key that should be evicted next.
    // Precondition: the policy is tracking at least one key.
    virtual std::string evictionCandidate() const = 0;

    // Removes a key from the policy's internal tracking (the key is being
    // deleted from the cache, whether by eviction, DEL, or lazy expiry).
    virtual void remove(const std::string& key) = 0;
};

// ---------------------------------------------------------------------------
// LRUPolicy — Least Recently Used eviction, O(1) for every operation.
//
// How it works:
//   - A doubly-linked list (std::list) keeps keys ordered by recency.
//     The FRONT of the list is the most-recently-used key; the BACK is the
//     least-recently-used key and therefore the eviction candidate.
//
//   - A hash map (std::unordered_map) maps each key to its iterator in the
//     list so we can find and move any node in O(1) without scanning.
//
// On access or insert we splice (move) the node to the front of the list.
// On eviction we simply look at the back.
// ---------------------------------------------------------------------------
class LRUPolicy : public EvictionPolicy {
public:
    // Record that an existing key was just accessed — move it to the front
    // of the recency list so it becomes the "most recently used."
    void recordAccess(const std::string& key) override {
        auto it = keyToIterator_.find(key);
        if (it == keyToIterator_.end()) {
            return;  // Key not tracked (shouldn't happen, but be safe).
        }
        // std::list::splice moves a node from one position to another in O(1)
        // without copying or invalidating iterators.  Here we move the node
        // pointed to by it->second to the front of the same list.
        order_.splice(order_.begin(), order_, it->second);
    }

    // Record a brand-new insertion — put the key at the front (most recent).
    void recordInsert(const std::string& key) override {
        // If the key is already tracked (this shouldn't normally happen
        // because Cache checks first), remove it so we don't get duplicates.
        auto existing = keyToIterator_.find(key);
        if (existing != keyToIterator_.end()) {
            order_.erase(existing->second);
            keyToIterator_.erase(existing);
        }
        order_.push_front(key);
        keyToIterator_[key] = order_.begin();
    }

    // The least-recently-used key sits at the BACK of the list.
    std::string evictionCandidate() const override {
        return order_.back();
    }

    // Remove a key entirely from the tracking structures.
    void remove(const std::string& key) override {
        auto it = keyToIterator_.find(key);
        if (it == keyToIterator_.end()) {
            return;
        }
        order_.erase(it->second);
        keyToIterator_.erase(it);
    }

private:
    // Doubly-linked list: front = most recent, back = least recent.
    std::list<std::string> order_;

    // Hash map from key → iterator into `order_` for O(1) lookup.
    std::unordered_map<std::string, std::list<std::string>::iterator> keyToIterator_;
};

#endif // EVICTION_POLICY_H
