#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "Cache.h"
#include "EvictionPolicy.h"
#include "PersistenceManager.h"

// ---------------------------------------------------------------------------
// main — a simple interactive command loop that demonstrates every feature:
//
//   SET key value [ttl]   — store a key; optional TTL in seconds
//   GET key               — retrieve a key (shows lazy expiration)
//   DEL key               — delete a key
//   EXIT                  — save a snapshot and quit
//
// On startup the cache loads any previously-saved snapshot, so it picks up
// where it left off after the last EXIT.
// ---------------------------------------------------------------------------
int main() {
    const size_t CAPACITY = 3;  // Small on purpose so eviction is easy to demo.
    const std::string SNAPSHOT_FILE = "mini_redis_snapshot.csv";

    // Construct the cache with an LRU eviction policy and file persistence.
    Cache cache(
        CAPACITY,
        std::make_unique<LRUPolicy>(),
        std::make_unique<PersistenceManager>(SNAPSHOT_FILE)
    );

    // Try to reload state from a previous session.
    cache.loadSnapshot();

    std::cout << "\n=== Redite ===\n"
              << "Capacity: " << CAPACITY << " entries, LRU eviction\n"
              << "Commands:\n"
              << "  SET key value [ttl]   — store a key (ttl in seconds, optional)\n"
              << "                          use quotes for multi-word values: SET k \"hello world\"\n"
              << "  GET key               — retrieve a key\n"
              << "  DEL key               — delete a key\n"
              << "  EXIT                  — save snapshot and quit\n\n";

    std::string line;
    while (true) {
        std::cout << "redite> ";
        if (!std::getline(std::cin, line)) {
            break;  // EOF (e.g. Ctrl+D / Ctrl+Z).
        }

        // Tokenize the input line.
        std::istringstream iss(line);
        std::string command;
        if (!(iss >> command)) {
            continue;  // Blank line — just show the prompt again.
        }

        // --- SET key value [ttl] ------------------------------------------
        // Bug 3 fix: values can be double-quoted to allow spaces, e.g.:
        //   SET greeting "hello world" 10
        // Unquoted values are still single-token. Trailing garbage after
        // the value (or after the TTL) now produces a warning.
        if (command == "SET" || command == "set") {
            std::string key;
            if (!(iss >> key)) {
                std::cout << "  Usage: SET key value [ttl]\n";
                continue;
            }

            // Skip whitespace, then check if value is quoted.
            std::string value;
            char ch;
            iss >> std::ws;  // Eat leading whitespace.
            if (!iss.get(ch)) {
                std::cout << "  Usage: SET key value [ttl]\n";
                continue;
            }

            if (ch == '"') {
                // Read until the closing quote.
                if (!std::getline(iss, value, '"')) {
                    std::cout << "  Error: unterminated quote.\n";
                    continue;
                }
            } else {
                // Unquoted: put the char back, read one token.
                iss.putback(ch);
                if (!(iss >> value)) {
                    std::cout << "  Usage: SET key value [ttl]\n";
                    continue;
                }
            }

            std::optional<int> ttl = std::nullopt;
            int ttlRaw;
            if (iss >> ttlRaw) {
                ttl = ttlRaw;
            } else {
                // Clear fail state so we can check for leftover tokens.
                // If iss >> ttlRaw failed, whatever was there wasn't an int.
                iss.clear();
            }

            // Warn if there's leftover text after value/TTL.
            std::string leftover;
            if (iss >> leftover) {
                std::cout << "  Warning: ignored trailing input: \""
                          << leftover;
                std::string rest;
                if (std::getline(iss, rest)) {
                    std::cout << rest;
                }
                std::cout << "\"\n";
            }

            cache.put(key, value, ttl);
            std::cout << "  OK";
            if (ttl.has_value()) {
                std::cout << " (TTL: " << ttl.value() << "s)";
            }
            std::cout << "  [size=" << cache.size() << "]\n";
        }

        // --- GET key ------------------------------------------------------
        else if (command == "GET" || command == "get") {
            std::string key;
            if (!(iss >> key)) {
                std::cout << "  Usage: GET key\n";
                continue;
            }

            auto result = cache.get(key);
            if (result.has_value()) {
                std::cout << "  \"" << result.value() << "\"\n";
            } else {
                std::cout << "  (nil)\n";
            }
        }

        // --- DEL key ------------------------------------------------------
        else if (command == "DEL" || command == "del") {
            std::string key;
            if (!(iss >> key)) {
                std::cout << "  Usage: DEL key\n";
                continue;
            }

            if (cache.del(key)) {
                std::cout << "  Deleted.  [size=" << cache.size() << "]\n";
            } else {
                std::cout << "  Key not found.\n";
            }
        }

        // --- EXIT ---------------------------------------------------------
        else if (command == "EXIT" || command == "exit") {
            cache.saveSnapshot();
            std::cout << "  Bye!\n";
            break;
        }

        // --- Unknown command ----------------------------------------------
        else {
            std::cout << "  Unknown command. Try SET, GET, DEL, or EXIT.\n";
        }
    }

    return 0;
}
