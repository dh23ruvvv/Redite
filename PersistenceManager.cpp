#include "PersistenceManager.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

PersistenceManager::PersistenceManager(const std::string& filePath)
    : filePath_(filePath) {}

// --- Escaping -----------------------------------------------------------------
// We only need to worry about two characters:
//   comma  →  \,      (because comma is our field separator)
//   backslash → \\    (so we can distinguish literal backslashes from escapes)

std::string PersistenceManager::escape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == ',') {
            out += "\\,";
        } else {
            out += c;
        }
    }
    return out;
}

std::string PersistenceManager::unescape(const std::string& escaped) {
    std::string out;
    out.reserve(escaped.size());
    for (size_t i = 0; i < escaped.size(); ++i) {
        if (escaped[i] == '\\' && i + 1 < escaped.size()) {
            // The next character is the literal that was escaped.
            out += escaped[i + 1];
            ++i;  // Skip the character after the backslash.
        } else {
            out += escaped[i];
        }
    }
    return out;
}

// Split a CSV line on commas that are NOT preceded by a backslash.
std::vector<std::string> PersistenceManager::splitCSVLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ',' && (i == 0 || line[i - 1] != '\\')) {
            fields.push_back(current);
            current.clear();
        } else {
            current += line[i];
        }
    }
    fields.push_back(current);  // Last field (no trailing comma).
    return fields;
}

// --- Snapshot (save) ----------------------------------------------------------

void PersistenceManager::snapshot(
        const std::unordered_map<std::string, CacheEntry>& entries) const {
    std::ofstream file(filePath_, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "[PersistenceManager] Could not open file for writing: "
                  << filePath_ << "\n";
        return;
    }

    // Header line.
    file << "key,value,remainingTTLSeconds\n";

    auto now = std::chrono::steady_clock::now();

    for (const auto& [k, entry] : entries) {
        // Skip entries that have already expired — no point saving them.
        if (entry.isExpired()) {
            continue;
        }

        // Calculate remaining TTL in whole seconds (-1 = no expiry).
        long long remainingTTL = -1;
        if (entry.expiresAt.has_value()) {
            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                entry.expiresAt.value() - now);
            remainingTTL = remaining.count();
            if (remainingTTL <= 0) {
                continue;  // Effectively expired — skip.
            }
        }

        file << escape(entry.key) << ","
             << escape(entry.value) << ","
             << remainingTTL << "\n";
    }

    std::cout << "[PersistenceManager] Snapshot saved to " << filePath_
              << " (" << entries.size() << " entries checked)\n";
}

// --- Load snapshot (restore) --------------------------------------------------

std::vector<CacheEntry> PersistenceManager::loadSnapshot() const {
    std::vector<CacheEntry> results;

    std::ifstream file(filePath_);
    if (!file.is_open()) {
        // File doesn't exist yet — that's fine on first run.
        return results;
    }

    std::string line;

    // Skip the header line.
    if (!std::getline(file, line)) {
        return results;
    }

    auto now = std::chrono::steady_clock::now();

    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> fields = splitCSVLine(line);
        if (fields.size() != 3) {
            std::cerr << "[PersistenceManager] Skipping malformed line: "
                      << line << "\n";
            continue;
        }

        CacheEntry entry;
        entry.key   = unescape(fields[0]);
        entry.value = unescape(fields[1]);
        entry.insertedAt = now;

        long long ttl = std::stoll(fields[2]);
        if (ttl > 0) {
            entry.expiresAt = now + std::chrono::seconds(ttl);
        }
        // ttl == -1 (or <= 0) means no expiration → expiresAt stays nullopt.

        results.push_back(std::move(entry));
    }

    std::cout << "[PersistenceManager] Loaded " << results.size()
              << " entries from " << filePath_ << "\n";
    return results;
}
