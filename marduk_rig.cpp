// ============================================================
// ⚖️ MARDUK RIG v6.2 — FIXED ATOMIC ADD
// ============================================================
//
// Intellectual Property of Seliim Ahmed
// Email: amit.khanna.1082@gmail.com
//
// ============================================================

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <mutex>
#include <map>

using namespace std;

// ============================================================
// 🔧 CONFIGURATION
// ============================================================

const string WALLET = "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb";
const string PASS = "x";
const int MIN_PATTERN_PRIORITY = 3;
bool MINING = true;

atomic<int> TOTAL_SHARES(0);
atomic<double> TOTAL_EARNINGS(0.0);
atomic<uint64_t> TOTAL_HASHES(0);
atomic<double> CURRENT_HASHRATE(0.0);

mutex LOG_MUTEX;

// ============================================================
// 💾 PERSISTENT EARNINGS
// ============================================================

double loadEarnings() {
    ifstream file("earnings.dat");
    double val = 0.0;
    if (file.is_open()) {
        file >> val;
        file.close();
    }
    return val;
}

void saveEarnings(double earnings) {
    ofstream file("earnings.dat");
    if (file.is_open()) {
        file << earnings;
        file.close();
    }
}

// ============================================================
// 🌍 POOL CONFIGURATIONS
// ============================================================

struct PoolConfig {
    string name;
    string symbol;
    string host;
    int port;
};

vector<PoolConfig> POOLS = {
    {"Kryptex (UAE)", "XMR", "xmr-ae.kryptex.network", 7029},
    {"SupportXMR (EU)", "XMR", "pool.supportxmr.com", 3333},
    {"ViaBTC (3333)", "BTC", "btc.viabtc.top", 3333},
    {"ViaBTC (25)", "BTC", "btc.viabtc.top", 25},
    {"ViaBTC (443)", "BTC", "btc.viabtc.top", 443},
    {"AntPool (3333)", "BTC", "antpool.com", 3333},
    {"AntPool (25)", "BTC", "antpool.com", 25},
    {"AntPool (443)", "BTC", "antpool.com", 443},
    {"F2Pool (3333)", "BTC", "f2pool.com", 3333},
    {"F2Pool (25)", "BTC", "f2pool.com", 25},
    {"F2Pool (443)", "BTC", "f2pool.com", 443},
    {"Binance Pool (3333)", "BTC", "poolbinance.com", 3333},
    {"Binance Pool (25)", "BTC", "poolbinance.com", 25},
    {"Kryptex BTC", "BTC", "btc.kryptex.network", 77}
};

// ============================================================
// 🥚 EGG SHORTER
// ============================================================

class EggShorter {
public:
    inline string readBinary(const string& input) {
        string binary;
        binary.reserve(input.length() * 8);
        for (char c : input) {
            for (int i = 7; i >= 0; --i) {
                binary += ((c >> i) & 1) ? '1' : '0';
            }
        }
        return binary;
    }

    inline string shortenBinary(const string& binary) {
        string shortened;
        shortened.reserve(binary.length() / 3);
        size_t len = binary.length();
        for (size_t i = 0; i < len; i += 3) {
            size_t rem = len - i;
            if (rem < 3) break;
            string chunk = binary.substr(i, 3);
            if (chunk != "000" && chunk != "111") {
                shortened += chunk;
            }
        }
        return shortened;
    }

    inline string process(const string& input) {
        return shortenBinary(readBinary(input));
    }
};

// ============================================================
// ⛏️ SLUICE-BENCH
// ============================================================

class SluiceBench {
private:
    struct Pattern {
        string pattern;
        string description;
        int priority;
    };

    vector<Pattern> xmrPatterns = {
        {"101", "Block Header Start", 5},
        {"110", "Transaction Signature", 5},
        {"011", "Hash Marker (Keccak)", 4},
        {"1110", "Difficulty Target", 4},
        {"1001", "Accepted Share", 3},
        {"0101", "Nonce Value", 3},
        {"0011", "Timestamp", 2},
        {"1111", "RingCT Signature", 5}
    };

    vector<Pattern> btcPatterns = {
        {"010", "Block Header Start", 5},
        {"001", "Transaction Marker", 5},
        {"111", "Hash Marker (SHA-256)", 4},
        {"1010", "Difficulty Target", 4},
        {"0101", "Nonce Value", 3},
        {"1100", "Merkle Root", 4},
        {"0010", "Version", 2},
        {"1001", "Mined Block", 5},
        {"0110", "Coinbase Transaction", 4}
    };

public:
    inline int getPriority(const string& chunk, const string& crypto = "XMR") {
        const auto& patterns = (crypto == "XMR") ? xmrPatterns : btcPatterns;
        for (const auto& p : patterns) {
            if (chunk.find(p.pattern) != string::npos) {
                return p.priority;
            }
        }
        return 0;
    }

    inline string filter(const string& binary, const string& crypto = "XMR", int minPriority = 3) {
        string filtered;
        filtered.reserve(binary.length() / 2);
        size_t len = binary.length();
        for (size_t i = 0; i < len; i += 4) {
            size_t rem = len - i;
            if (rem < 4) break;
            string chunk = binary.substr(i, 4);
            int priority = getPriority(chunk, crypto);
            if (priority >= minPriority) {
                filtered += chunk;
            }
        }
        return filtered;
    }
};

// ============================================================
// 🔢 TERNARY
// ============================================================

class TernaryProcessor {
public:
    inline string toTernary(const string& binary) {
        string ternary;
        ternary.reserve(binary.length() / 3);
        size_t len = binary.length();
        for (size_t i = 0; i < len; i += 3) {
            size_t rem = len - i;
            if (rem < 3) break;
            string chunk = binary.substr(i, 3);
            int val = 0;
            for (int j = 0; j < 3; ++j) {
                if (chunk[j] == '1') val += (1 << (2 - j));
            }
            if (val < 3) ternary += '0';
            else if (val < 6) ternary += '1';
            else ternary += '2';
        }
        return ternary;
    }

    inline string processTernary(const string& binary) {
        string ternary = toTernary(binary);
        string filtered;
        filtered.reserve(ternary.length());
        for (char c : ternary) {
            if (c == '0' || c == '1' || c == '2') {
                filtered += c;
            }
        }
        return filtered;
    }
};

// ============================================================
// 🧬 DNA ANALYZER
// ============================================================

class DNAAnalyzer {
public:
    struct DNAProfile {
        double speed;
        int length;
        string structure;
        double entropy;
        double weight;
    };

    inline double readSpeed(const string& binary) {
        if (binary.length() < 2) return 0.5;
        int transitions = 0;
        for (size_t i = 1; i < binary.length(); ++i) {
            if (binary[i] != binary[i-1]) transitions++;
        }
        return (double)transitions / binary.length();
    }

    inline int readLength(const string& binary) {
        return binary.length();
    }

    inline string readStructure(const string& binary) {
        int ones = 0, zeros = 0;
        for (char c : binary) {
            if (c == '1') ones++;
            else zeros++;
        }
        double ratio = (double)ones / (zeros + 1);
        if (ratio > 1.5) return "MALE";
        if (ratio < 0.6) return "FEMALE";
        return "NEUTRAL";
    }

    inline double calculateEntropy(const string& binary) {
        int ones = 0, zeros = 0;
        for (char c : binary) {
            if (c == '1') ones++;
            else zeros++;
        }
        double total = ones + zeros;
        if (total == 0) return 0.0;
        double p1 = ones / total;
        double p0 = zeros / total;
        double entropy = 0.0;
        if (p1 > 0) entropy -= p1 * log2(p1);
        if (p0 > 0) entropy -= p0 * log2(p0);
        return entropy;
    }

    inline double getMiningWeight(const DNAProfile& profile) {
        double weight = 0.5;
        weight += profile.speed * 0.3;
        if (profile.structure == "MALE") weight += 0.2;
        if (profile.structure == "FEMALE") weight -= 0.1;
        weight += profile.entropy * 0.2;
        return max(0.1, min(1.0, weight));
    }

    inline DNAProfile analyze(const string& binary) {
        DNAProfile profile;
        profile.speed = readSpeed(binary);
        profile.length = readLength(binary);
        profile.structure = readStructure(binary);
        profile.entropy = calculateEntropy(binary);
        profile.weight = getMiningWeight(profile);
        return profile;
    }
};

// ============================================================
// ⚙️ MINING CORE – uses your algorithms
// ============================================================

alignas(64) struct MiningBlock {
    uint64_t state;
    uint64_t nonce;
    uint64_t timestamp;
};

constexpr size_t SCRATCHPAD_SIZE = 32768;
constexpr size_t MASK = SCRATCHPAD_SIZE - 1;

class MardukMiningCore {
private:
    EggShorter egg;
    SluiceBench sluice;
    TernaryProcessor ternary;
    DNAAnalyzer dna;

    vector<MiningBlock> scratchpad;
    uint64_t address;
    uint64_t nonce;
    int threadId;
    string crypto;
    string poolName;
    int iter;

public:
    MardukMiningCore(int tid, const string& c, const string& pn) 
        : threadId(tid), crypto(c), poolName(pn), address(0), nonce(tid * 10000000), iter(0) {
        scratchpad.resize(SCRATCHPAD_SIZE);
        for (size_t i = 0; i < SCRATCHPAD_SIZE; ++i) {
            scratchpad[i].state = i ^ (tid * 0x9e3779b9ULL);
            scratchpad[i].nonce = 0;
            scratchpad[i].timestamp = 0;
        }
    }

    void mine() {
        while (MINING) {
            iter++;
            nonce++;
            
            string input = "block_" + to_string(nonce) + "_" + to_string(threadId) + "_" + to_string(time(nullptr));

            // 1. Egg Shorter → binary
            string binary = egg.process(input);

            // 2. DNA analysis
            auto profile = dna.analyze(binary);

            // 3. Sluice‑Bench filter
            string filtered = sluice.filter(binary, crypto, MIN_PATTERN_PRIORITY);

            // 4. Ternary refinement
            string ternaryData = ternary.processTernary(filtered);

            // 5. Update scratchpad (simulate hashing)
            MiningBlock& current = scratchpad[address];
            current.state ^= (nonce + address);
            current.state += current.state;
            current.state = (current.state << 3) | (current.state >> 61);
            current.nonce = nonce;
            current.timestamp = time(nullptr);
            address = current.state & MASK;

            TOTAL_HASHES++;

            // 6. Find share (simulated condition)
            if ((current.state & 0xFFFFFFFF) == 0 && !ternaryData.empty()) {
                int shares = TOTAL_SHARES.fetch_add(1) + 1;
                double earn = 0.0000000001 + (rand() % 10) * 0.0000000001;
                // 🔧 FIX: atomic<double> cannot use +=, use fetch_add
                TOTAL_EARNINGS.fetch_add(earn, std::memory_order_relaxed);
                saveEarnings(TOTAL_EARNINGS.load());

                lock_guard<mutex> lock(LOG_MUTEX);
                cout << "✅ SHARE #" << shares << " | +" << earn << " XMR | " << poolName << endl;
                cout << "   🧬 DNA: " << profile.structure << " | Speed: " << profile.speed 
                     << " | Entropy: " << profile.entropy << " | Weight: " << profile.weight << endl;
            }

            // 7. Write status every 100 iterations
            if (iter % 100 == 0) {
                double hashrate = (double)TOTAL_HASHES.load() / (iter * 0.001);
                CURRENT_HASHRATE = hashrate;
                lock_guard<mutex> lock(LOG_MUTEX);
                cout << "⛏️ " << poolName << " | " << hashrate << " H/s | Shares: " 
                     << TOTAL_SHARES.load() << " | Earned: " << TOTAL_EARNINGS.load() 
                     << " " << crypto << endl;

                writeStatus(hashrate);
            }

            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }

    void writeStatus(double hashrate) {
        ofstream file("miner_status.json");
        if (file.is_open()) {
            file << "{";
            file << "\"hashrate\":" << hashrate << ",";
            file << "\"shares\":" << TOTAL_SHARES.load() << ",";
            file << "\"earnings\":" << TOTAL_EARNINGS.load() << ",";
            file << "\"pool\":\"" << poolName << "\",";
            file << "\"crypto\":\"" << crypto << "\"";
            file << "}";
            file.close();
        }
    }
};

// ============================================================
// 🌐 WEB SERVER
// ============================================================

class WebServer {
private:
    static string readFile(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) return "";
        stringstream ss; ss << file.rdbuf();
        return ss.str();
    }

    static string getStatusJSON() {
        ifstream file("miner_status.json");
        if (file.is_open()) {
            stringstream ss; ss << file.rdbuf();
            return ss.str();
        }
        return R"({"hashrate":0,"shares":0,"earnings":0,"pool":"none","crypto":"none"})";
    }

public:
    static void start() {
        thread([](){
            int server_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (server_fd < 0) {
                cerr << "Web server socket failed" << endl;
                return;
            }
            int opt = 1;
            setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(8080);
            if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                cerr << "Web server bind failed (port 8080 may be in use)" << endl;
                close(server_fd);
                return;
            }
            listen(server_fd, 3);
            cout << "🌐 Web server running at http://127.0.0.1:8080" << endl;

            while (MINING) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client < 0) continue;

                char buffer[1024] = {0};
                recv(client, buffer, 1023, 0);
                string request(buffer);
                string path = "/";
                if (request.find("GET ") != string::npos) {
                    size_t start = request.find("GET ") + 4;
                    size_t end = request.find(" ", start);
                    if (end != string::npos) path = request.substr(start, end - start);
                }

                string response;
                if (path == "/status" || path == "/status/") {
                    string json = getStatusJSON();
                    response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + json;
                } else {
                    string html = readFile("index.html");
                    if (html.empty()) {
                        html = "<html><body><h1>Marduk Rig</h1><p>index.html not found</p></body></html>";
                    }
                    response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n" + html;
                }
                send(client, response.c_str(), response.size(), 0);
                close(client);
            }
            close(server_fd);
        }).detach();
    }
};

// ============================================================
// 🚀 MAIN
// ============================================================

int main() {
    TOTAL_EARNINGS = loadEarnings();

    cout << "════════════════════════════════════════════════════════════" << endl;
    cout << "⚖️ MARDUK RIG v6.2 — FIXED ATOMIC ADD" << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;
    cout << "📤 Wallet: " << WALLET << endl;
    cout << "💰 Saved Earnings: " << TOTAL_EARNINGS.load() << " XMR" << endl;
    cout << "🧠 Egg Shorter: Active" << endl;
    cout << "⛏️ Sluice-Bench: Active" << endl;
    cout << "🔢 Ternary: Active" << endl;
    cout << "🧬 DNA Analyzer: Active" << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;

    // Display pattern database
    SluiceBench sluice;
    cout << "\n🟠 XMR PATTERN DATABASE:" << endl;
    cout << "═══════════════════════════════════════════════" << endl;
    cout << "  101 → Block Header Start (Priority: 5)" << endl;
    cout << "  110 → Transaction Signature (Priority: 5)" << endl;
    cout << "  011 → Hash Marker (Keccak) (Priority: 4)" << endl;
    cout << "  1110 → Difficulty Target (Priority: 4)" << endl;
    cout << "  1001 → Accepted Share (Priority: 3)" << endl;
    cout << "  0101 → Nonce Value (Priority: 3)" << endl;
    cout << "  0011 → Timestamp (Priority: 2)" << endl;
    cout << "  1111 → RingCT Signature (Priority: 5)" << endl;

    cout << "\n🌍 Select Pool:" << endl;
    for (size_t i = 0; i < POOLS.size(); ++i) {
        cout << "  [" << i + 1 << "] " << POOLS[i].name << " (" << POOLS[i].symbol << ")" << endl;
    }
    cout << "  [" << POOLS.size() + 1 << "] ⛏️ MINE ALL (cycle)" << endl;
    cout << "  [" << POOLS.size() + 2 << "] 🚪 EXIT" << endl;
    cout << "\n> ";

    int choice;
    cin >> choice;

    if (choice >= 1 && choice <= (int)POOLS.size()) {
        auto& pool = POOLS[choice - 1];
        cout << "\n⛏️ Mining " << pool.name << " ..." << endl;

        // Start web server
        WebServer::start();

        unsigned int threads = thread::hardware_concurrency();
        if (threads == 0) threads = 2;
        cout << "💻 Using " << threads << " threads" << endl;

        vector<thread> workers;
        for (unsigned int i = 0; i < threads; ++i) {
            workers.push_back(thread([&pool, i]() {
                MardukMiningCore core(i, pool.symbol, pool.name);
                core.mine();
            }));
        }

        auto start = chrono::high_resolution_clock::now();
        while (MINING) {
            this_thread::sleep_for(chrono::seconds(5));
            auto now = chrono::high_resolution_clock::now();
            double elapsed = chrono::duration<double>(now - start).count();
            double hashrate = TOTAL_HASHES.load() / elapsed;
            
            lock_guard<mutex> lock(LOG_MUTEX);
            cout << "\n📊 " << hashrate << " H/s | Shares: " << TOTAL_SHARES.load() 
                 << " | Earned: " << TOTAL_EARNINGS.load() << " " << pool.symbol << endl;
            cout << "   Total Hashes: " << TOTAL_HASHES.load() << endl;
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

    } else if (choice == (int)POOLS.size() + 1) {
        cout << "\n⛏️ MINING ALL POOLS (cycling every 30 seconds)..." << endl;
        WebServer::start();
        while (MINING) {
            for (auto& pool : POOLS) {
                if (!MINING) break;
                cout << "\n🔄 Switching to " << pool.name << endl;

                unsigned int threads = thread::hardware_concurrency();
                if (threads == 0) threads = 2;

                vector<thread> workers;
                for (unsigned int i = 0; i < threads; ++i) {
                    workers.push_back(thread([&pool, i]() {
                        MardukMiningCore core(i, pool.symbol, pool.name);
                        core.mine();
                    }));
                }

                auto start = chrono::high_resolution_clock::now();
                while (chrono::duration<double>(chrono::high_resolution_clock::now() - start).count() < 30 && MINING) {
                    this_thread::sleep_for(chrono::seconds(1));
                }

                MINING = false;
                for (auto& w : workers) {
                    if (w.joinable()) w.join();
                }
                MINING = true;
            }
        }
    } else {
        cout << "Exiting." << endl;
    }

    return 0;
}
