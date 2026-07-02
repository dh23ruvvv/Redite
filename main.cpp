#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Cache.h"
#include "EvictionPolicy.h"
#include "PersistenceManager.h"

// ---------------------------------------------------------------------------
// Helper: convert a name to lowercase so commands are case-insensitive
// for user names (alice == Alice == ALICE).
// ---------------------------------------------------------------------------
static std::string toLower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Helper: check if a user is registered in the cache.
// ---------------------------------------------------------------------------
static bool userExists(Cache& cache, const std::string& name) {
    return cache.get("user:" + name).has_value();
}

// ---------------------------------------------------------------------------
// Helper: read the current debt that `debtor` owes `creditor`.
// Returns 0.0 if no debt exists.
// ---------------------------------------------------------------------------
static double getDebt(Cache& cache, const std::string& debtor,
                      const std::string& creditor) {
    auto val = cache.get("debt:" + debtor + ":" + creditor);
    if (!val.has_value()) {
        return 0.0;
    }
    try {
        return std::stod(val.value());
    } catch (...) {
        return 0.0;
    }
}

// ---------------------------------------------------------------------------
// Helper: set the debt that `debtor` owes `creditor`.
// If the amount is effectively zero, delete the key to keep things clean.
// ---------------------------------------------------------------------------
static void setDebt(Cache& cache, const std::string& debtor,
                    const std::string& creditor, double amount) {
    std::string key = "debt:" + debtor + ":" + creditor;
    if (amount < 0.01) {
        cache.del(key);
    } else {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << amount;
        cache.put(key, oss.str());
    }
}

// ---------------------------------------------------------------------------
// Helper: update the net debt between two people.
//
// If debtor already owes creditor, we add to that.
// If creditor owes debtor (reverse direction), we subtract first to net out.
// Example: if alice owes bob 50, and now bob owes alice 100, the result is
// bob owes alice 50 (the alice→bob debt is cleared, bob→alice debt = 50).
// ---------------------------------------------------------------------------
static void addDebt(Cache& cache, const std::string& debtor,
                    const std::string& creditor, double amount) {
    // Check the reverse direction first.
    double reverse = getDebt(cache, creditor, debtor);
    if (reverse > 0.0) {
        if (amount <= reverse) {
            // The new debt is smaller — just reduce the reverse debt.
            setDebt(cache, creditor, debtor, reverse - amount);
            return;
        } else {
            // The new debt exceeds the reverse — clear reverse, carry remainder.
            setDebt(cache, creditor, debtor, 0.0);
            amount -= reverse;
        }
    }

    // Add to (or create) the forward debt.
    double forward = getDebt(cache, debtor, creditor);
    setDebt(cache, debtor, creditor, forward + amount);
}

// ---------------------------------------------------------------------------
// Helper: get the next log entry number.
// We store the count in a special key "log:count".
// ---------------------------------------------------------------------------
static int getLogCount(Cache& cache) {
    auto val = cache.get("log:count");
    if (!val.has_value()) return 0;
    try {
        return std::stoi(val.value());
    } catch (...) {
        return 0;
    }
}

static void appendLog(Cache& cache, const std::string& message) {
    int count = getLogCount(cache);
    cache.put("log:" + std::to_string(count), message);
    cache.put("log:count", std::to_string(count + 1));
}

// ---------------------------------------------------------------------------
// main — Mini Splitwise, powered by the Redite key-value engine.
//
// Commands:
//   ADD <name>                              — register a person
//   EXPENSE <payer> <amount> <p1> <p2> ...  — split a bill equally
//   BALANCES                                — show who owes whom
//   SETTLE <debtor> <creditor>              — clear a debt
//   HISTORY                                 — show expense log
//   EXIT                                    — save and quit
// ---------------------------------------------------------------------------
int main() {
    const size_t CAPACITY = 1000;
    const std::string SNAPSHOT_FILE = "splitwise_snapshot.csv";

    Cache cache(
        CAPACITY,
        std::make_unique<LRUPolicy>(),
        std::make_unique<PersistenceManager>(SNAPSHOT_FILE)
    );

    cache.loadSnapshot();

    std::cout << "\n"
              << "=== Mini Splitwise ===\n"
              << "Powered by the Redite key-value engine\n\n"
              << "Commands:\n"
              << "  ADD <name>                              — register a person\n"
              << "  EXPENSE <payer> <amount> <p1> <p2> ...  — split a bill equally\n"
              << "  BALANCES                                — show who owes whom\n"
              << "  SETTLE <debtor> <creditor>              — clear a debt\n"
              << "  HISTORY                                 — show expense log\n"
              << "  EXIT                                    — save and quit\n\n";

    std::string line;
    while (true) {
        std::cout << "splitwise> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        std::istringstream iss(line);
        std::string command;
        if (!(iss >> command)) {
            continue;  // Blank line.
        }

        // Uppercase the command for case-insensitive matching.
        std::string cmd = toLower(command);

        // --- ADD <name> ---------------------------------------------------
        if (cmd == "add") {
            std::string name;
            if (!(iss >> name)) {
                std::cout << "  Usage: ADD <name>\n";
                continue;
            }
            name = toLower(name);

            if (userExists(cache, name)) {
                std::cout << "  \"" << name << "\" is already in the group.\n";
                continue;
            }

            cache.put("user:" + name, "1");
            std::cout << "  Added " << name << ".\n";
        }

        // --- EXPENSE <payer> <amount> <p1> <p2> ... ----------------------
        else if (cmd == "expense") {
            std::string payer;
            double amount;
            if (!(iss >> payer >> amount)) {
                std::cout << "  Usage: EXPENSE <payer> <amount> <p1> <p2> ...\n";
                continue;
            }
            payer = toLower(payer);

            if (!userExists(cache, payer)) {
                std::cout << "  Error: \"" << payer << "\" is not in the group. Use ADD first.\n";
                continue;
            }
            if (amount <= 0) {
                std::cout << "  Error: amount must be positive.\n";
                continue;
            }

            // Read all participants.
            std::vector<std::string> participants;
            std::string p;
            while (iss >> p) {
                participants.push_back(toLower(p));
            }

            if (participants.empty()) {
                std::cout << "  Usage: EXPENSE <payer> <amount> <p1> <p2> ...\n";
                std::cout << "  (list the people sharing the bill, including the payer)\n";
                continue;
            }

            // Validate all participants are registered.
            bool allValid = true;
            for (const auto& person : participants) {
                if (!userExists(cache, person)) {
                    std::cout << "  Error: \"" << person << "\" is not in the group. Use ADD first.\n";
                    allValid = false;
                }
            }
            if (!allValid) continue;

            double perPerson = amount / participants.size();

            // Build a log message.
            std::ostringstream logMsg;
            logMsg << std::fixed << std::setprecision(2);
            logMsg << payer << " paid " << amount << ", split " 
                   << participants.size() << " ways (" << perPerson << " each)";
            appendLog(cache, logMsg.str());

            std::cout << std::fixed << std::setprecision(2);
            std::cout << "  " << payer << " paid " << amount
                      << ", split " << participants.size()
                      << " ways (" << perPerson << " each).\n";

            // Update debts: everyone except the payer now owes the payer.
            for (const auto& person : participants) {
                if (person == payer) continue;
                addDebt(cache, person, payer, perPerson);
                std::cout << "  " << person << " owes " << payer
                          << " " << perPerson << "\n";
            }
        }

        // --- BALANCES ----------------------------------------------------
        else if (cmd == "balances") {
            const auto& all = cache.getAll();
            bool found = false;

            std::cout << std::fixed << std::setprecision(2);
            for (const auto& [key, entry] : all) {
                // Only look at keys starting with "debt:".
                if (key.rfind("debt:", 0) != 0) continue;

                // Parse "debt:debtor:creditor".
                std::string rest = key.substr(5);  // skip "debt:"
                size_t colon = rest.find(':');
                if (colon == std::string::npos) continue;

                std::string debtor = rest.substr(0, colon);
                std::string creditor = rest.substr(colon + 1);

                double amt = 0.0;
                try { amt = std::stod(entry.value); } catch (...) { continue; }
                if (amt < 0.01) continue;

                if (!found) {
                    std::cout << "\n  Outstanding balances:\n";
                    std::cout << "  ---------------------\n";
                    found = true;
                }
                std::cout << "  " << debtor << "  ->  " << creditor
                          << "  :  " << amt << "\n";
            }

            if (!found) {
                std::cout << "  All settled up! No outstanding debts.\n";
            }
            std::cout << "\n";
        }

        // --- SETTLE <debtor> <creditor> ----------------------------------
        else if (cmd == "settle") {
            std::string debtor, creditor;
            if (!(iss >> debtor >> creditor)) {
                std::cout << "  Usage: SETTLE <debtor> <creditor>\n";
                continue;
            }
            debtor = toLower(debtor);
            creditor = toLower(creditor);

            double amt = getDebt(cache, debtor, creditor);
            if (amt < 0.01) {
                std::cout << "  " << debtor << " doesn't owe " << creditor << " anything.\n";
                continue;
            }

            setDebt(cache, debtor, creditor, 0.0);

            std::ostringstream logMsg;
            logMsg << std::fixed << std::setprecision(2);
            logMsg << "Settled: " << debtor << " paid " << creditor << " " << amt;
            appendLog(cache, logMsg.str());

            std::cout << std::fixed << std::setprecision(2);
            std::cout << "  Settled! " << debtor << " no longer owes "
                      << creditor << " " << amt << ".\n";
        }

        // --- HISTORY -----------------------------------------------------
        else if (cmd == "history") {
            int count = getLogCount(cache);
            if (count == 0) {
                std::cout << "  No expenses recorded yet.\n";
                continue;
            }

            std::cout << "\n  Expense history:\n";
            std::cout << "  ----------------\n";
            for (int i = 0; i < count; ++i) {
                auto msg = cache.get("log:" + std::to_string(i));
                if (msg.has_value()) {
                    std::cout << "  " << (i + 1) << ". " << msg.value() << "\n";
                }
            }
            std::cout << "\n";
        }

        // --- EXIT --------------------------------------------------------
        else if (cmd == "exit") {
            cache.saveSnapshot();
            std::cout << "  Bye!\n";
            break;
        }

        // --- Unknown command ---------------------------------------------
        else {
            std::cout << "  Unknown command. Try ADD, EXPENSE, BALANCES, SETTLE, HISTORY, or EXIT.\n";
        }
    }

    return 0;
}
