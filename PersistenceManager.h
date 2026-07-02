#ifndef PERSISTENCE_MANAGER_H
#define PERSISTENCE_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>

#include "CacheEntry.h"

// ---------------------------------------------------------------------------
// PersistenceManager — saves and restores cache state to a human-readable
// CSV file so the cache survives across restarts.
//
// File format (one entry per line, first line is a header):
//   key,value,remainingTTLSeconds
//
// remainingTTLSeconds is -1 for keys with no expiration.  On reload, the
// TTL is re-applied from the current moment, so the key lives for roughly
// that many more seconds (not an exact wall-clock restore, but close enough
// for a demo and avoids serialising absolute time_points).
//
// Commas inside keys or values are escaped:  comma → \, and backslash → \\.
// This is minimal but keeps the file genuinely human-readable.
// ---------------------------------------------------------------------------
class PersistenceManager {
public:
    explicit PersistenceManager(const std::string& filePath);

    // Write every entry in `entries` to the snapshot file, overwriting any
    // previous snapshot.
    void snapshot(const std::unordered_map<std::string, CacheEntry>& entries) const;

    // Read the snapshot file and return the entries found.  Each returned
    // CacheEntry has its insertedAt set to "now" and expiresAt recalculated
    // from the stored remaining TTL.  Returns an empty vector if the file
    // doesn't exist or is empty.
    std::vector<CacheEntry> loadSnapshot() const;

private:
    std::string filePath_;

    // Escaping helpers for CSV serialization.
    static std::string escape(const std::string& raw);
    static std::string unescape(const std::string& escaped);

    // Split a line on un-escaped commas.
    static std::vector<std::string> splitCSVLine(const std::string& line);
};

#endif // PERSISTENCE_MANAGER_H
