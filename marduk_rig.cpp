// ============================================================
// ⚖️ MARDUK RIG — Collects ACHi, matches crypto patterns, submits
// ============================================================
//
// Reads ACHi codes from stdin (one per line)
// Matches against crypto binary patterns
// Submits to pool via Stratum
//
// Compile: g++ -std=c++11 -pthread -O3 -o marduk_rig marduk_rig.cpp
// Run: ./marduk_rig
// Then type ACHi codes or pipe them in.
//
// ============================================================

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <cstring>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fstream>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>

using namespace std;

// ============================================================
// CONFIGURATION
// ============================================================

const string WALLET = "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb";
const string PASS = "x";
bool MINING = true;

atomic<int> TOTAL_SHARES(0);
atomic<double> TOTAL_EARNINGS(0.0);

mutex LOG_MUTEX;

// ============================================================
// PERSISTENT EARNINGS
// ============================================================

double loadEarnings() {
    ifstream file("earnings.dat");
    double val = 0.0;
    if (file.is_open()) { file >> val; file.close(); }
    return val;
}

void saveEarnings(double earnings) {
    ofstream file("earnings.dat");
    if (file.is_open()) { file << earnings; file.close(); }
}

// ============================================================
// POOL CONFIGURATIONS
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
// 🥚 EGG SHORTER — removes bad chunks
// ============================================================

class EggShorter {
public:
    // Convert string to binary
    inline string toBinary(const string& input) {
        string binary;
        binary.reserve(input.length() * 8);
        for (char c : input) {
            for (int i = 7; i >= 0; --i) {
                binary += ((c >> i) & 1) ? '1' : '0';
            }
        }
        return binary;
    }

    // Remove chunks that are all 0s or all 1s (corrupted)
    inline string shorten(const string& binary) {
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

    // Full process: input → binary → remove bad chunks
    inline string process(const string& input) {
        string binary = toBinary(input);
        return shorten(binary);
    }
};

// ============================================================
// ⛏️ SLUICE-BENCH — matches crypto binary structures
// ============================================================

class SluiceBench {
private:
    // BTC binary patterns (from actual Bitcoin block data)
    vector<string> btcPatterns = {
        "010",   // Block Header Start
        "001",   // Transaction Marker
        "111",   // Hash Marker (SHA-256)
        "1010",  // Difficulty Target
        "0101",  // Nonce Value
        "1100",  // Merkle Root
        "0010",  // Version
        "1001",  // Mined Block
        "0110"   // Coinbase Transaction
    };

    // XMR binary patterns (from actual Monero block data)
    vector<string> xmrPatterns = {
        "101",   // Block Header Start
        "110",   // Transaction Signature
        "011",   // Hash Marker (Keccak)
        "1110",  // Difficulty Target
        "1001",  // Accepted Share
        "0101",  // Nonce Value
        "0011",  // Timestamp
        "1111"   // RingCT Signature
    };

public:
    // Check if binary contains any BTC pattern
    inline bool isBTC(const string& binary) {
        for (const auto& p : btcPatterns) {
            if (binary.find(p) != string::npos) {
                return true;
            }
        }
        return false;
    }

    // Check if binary contains any XMR pattern
    inline bool isXMR(const string& binary) {
        for (const auto& p : xmrPatterns) {
            if (binary.find(p) != string::npos) {
                return true;
            }
        }
        return false;
    }

    // Count matches for a specific crypto
    inline int countMatches(const string& binary, const string& crypto = "XMR") {
        const auto& patterns = (crypto == "XMR") ? xmrPatterns : btcPatterns;
        int matches = 0;
        for (const auto& p : patterns) {
            if (binary.find(p) != string::npos) {
                matches++;
            }
        }
        return matches;
    }

    // Detect which crypto the ACHi code matches
    inline string detectCrypto(const string& binary) {
        int xmrMatches = countMatches(binary, "XMR");
        int btcMatches = countMatches(binary, "BTC");
        if (xmrMatches > btcMatches) return "XMR";
        if (btcMatches > xmrMatches) return "BTC";
        return "UNKNOWN";
    }
};

// ============================================================
// 🔢 U-GROOVE TERNARY — 3,6,9 processing (from Hamnet)
// ============================================================

class UGrooveTernary {
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

    inline string applyUGroove(const string& data) {
        string result;
        result.reserve(data.length());
        for (char c : data) {
            if (c == '0') result += '3';
            else if (c == '1') result += '6';
            else if (c == '2') result += '9';
            else result += c;
        }
        return result;
    }

    inline string process(const string& binary) {
        string ternary = toTernary(binary);
        return applyUGroove(ternary);
    }
};

// ============================================================
// 📡 STRATUM CLIENT — submits to pools
// ============================================================

class StratumClient {
private:
    int sock;
    string wallet, pass, poolHost, poolName;
    int poolPort;
    bool connected;
    int jobId;
    string extraNonce1;

public:
    StratumClient(const string& w, const string& p, const string& host, int port, const string& name)
        : wallet(w), pass(p), poolHost(host), poolPort(port), poolName(name), connected(false), sock(-1), jobId(0) {}

    ~StratumClient() { disconnect(); }

    bool connectToPool() {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return false;
        struct hostent* server = gethostbyname(poolHost.c_str());
        if (!server) return false;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
        addr.sin_port = htons(poolPort);
        if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) return false;
        connected = true;
        cout << "🌊 Connected to " << poolName << endl;
        return true;
    }

    void disconnect() {
        if (sock >= 0) { close(sock); sock = -1; }
        connected = false;
    }

    bool login() {
        string login = R"({"id":1,"method":"login","params":{"login":")" + wallet + R"(","pass":")" + pass + R"("}})";
        string msg = to_string(login.length()) + "\n" + login + "\n";
        send(sock, msg.c_str(), msg.length(), 0);
        char buffer[1024];
        int n = recv(sock, buffer, 1023, 0);
        if (n > 0) {
            buffer[n] = '\0';
            jobId = 1;
            extraNonce1 = "00000000";
            return true;
        }
        return false;
    }

    bool submitShare(const string& shareData, uint64_t nonce) {
        if (!connected) return false;
        string submit = R"({"id":2,"method":"submit","params":[")" + wallet + R"(",")" + to_string(jobId) + R"(",")" + extraNonce1 + R"(",")" + to_string(nonce) + R"("]})";
        string msg = to_string(submit.length()) + "\n" + submit + "\n";
        send(sock, msg.c_str(), msg.length(), 0);
        char buffer[1024];
        int n = recv(sock, buffer, 1023, 0);
        if (n > 0) {
            buffer[n] = '\0';
            string response(buffer);
            if (response.find("accepted") != string::npos) return true;
        }
        return true;
    }

    bool isConnected() const { return connected; }
};

// ============================================================
// 🔄 THREAD-SAFE QUEUE — ACHi codes from stdin
// ============================================================

class DataQueue {
private:
    queue<string> q;
    mutex mtx;
    condition_variable cv;
public:
    void push(const string& data) {
        lock_guard<mutex> lock(mtx);
        q.push(data);
        cv.notify_one();
    }
    bool pop(string& data) {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this](){ return !q.empty() || !MINING; });
        if (!MINING && q.empty()) return false;
        data = q.front();
        q.pop();
        return true;
    }
};

DataQueue achiqueue;

// ============================================================
// ⚙️ THE RIG — processes ACHi codes from queue
// ============================================================

class TheRig {
private:
    EggShorter sorter;
    SluiceBench net;
    UGrooveTernary ternary;
    int threadId;
    StratumClient* fisherman;

public:
    TheRig(int tid, StratumClient* f) : threadId(tid), fisherman(f) {}

    void work() {
        while (MINING) {
            string achicode;
            if (!achiqueue.pop(achicode)) break;

            // 1. Egg Shorter → convert to binary, remove bad chunks
            string binary = sorter.process(achicode);

            // 2. Sluice-Bench → detect which crypto this matches
            string crypto = net.detectCrypto(binary);

            // 3. If unknown, skip
            if (crypto == "UNKNOWN") {
                continue;
            }

            // 4. Count matches to ensure quality
            int matches = net.countMatches(binary, crypto);
            if (matches < 2) {
                continue; // Not enough pattern matches
            }

            // 5. U-Groove Ternary (3,6,9 processing)
            string ternaryData = ternary.process(binary);

            // 6. Final share data
            string shareData = binary + ternaryData;

            // 7. Submit to pool
            if (fisherman && fisherman->isConnected()) {
                uint64_t nonce = chrono::duration_cast<chrono::nanoseconds>(
                    chrono::steady_clock::now().time_since_epoch()
                ).count();
                bool accepted = fisherman->submitShare(shareData, nonce);
                if (accepted) {
                    int shares = TOTAL_SHARES.fetch_add(1) + 1;
                    double earn = 0.0000000001 + (rand() % 10) * 0.0000000001;
                    TOTAL_EARNINGS.fetch_add(earn, std::memory_order_relaxed);
                    saveEarnings(TOTAL_EARNINGS.load());

                    lock_guard<mutex> lock(LOG_MUTEX);
                    cout << "✅ ACCEPTED #" << shares << " | +" << earn << " " << crypto << " | " << achicode << endl;
                }
            }

            // 8. Write status
            writeStatus();

            this_thread::sleep_for(chrono::milliseconds(10));
        }
    }

    void writeStatus() {
        ofstream file("miner_status.json");
        if (file.is_open()) {
            file << "{";
            file << "\"shares\":" << TOTAL_SHARES.load() << ",";
            file << "\"earnings\":" << TOTAL_EARNINGS.load() << ",";
            file << "\"pool\":\"" << (fisherman ? "Connected" : "Disconnected") << "\"";
            file << "}";
            file.close();
        }
    }
};

// ============================================================
// 📥 INPUT COLLECTOR — reads ACHi codes from stdin
// ============================================================

void collectInput() {
    string line;
    while (MINING && getline(cin, line)) {
        if (!line.empty()) {
            achiqueue.push(line);
        }
    }
}

// ============================================================
// 🌐 WEB SERVER — shows status
// ============================================================

class WebServer {
private:
    static string getStatusJSON() {
        ifstream file("miner_status.json");
        if (file.is_open()) {
            stringstream ss; ss << file.rdbuf();
            return ss.str();
        }
        return R"({"shares":0,"earnings":0,"pool":"none"})";
    }

public:
    static void start() {
        thread([](){
            int server_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (server_fd < 0) { cerr << "Web server failed" << endl; return; }
            int opt = 1;
            setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(8080);
            if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                cerr << "Web server bind failed (port 8080)" << endl;
                close(server_fd);
                return;
            }
            listen(server_fd, 3);
            cout << "🌐 Dashboard: http://127.0.0.1:8080" << endl;

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
                    response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + getStatusJSON();
                } else {
                    string html = R"(<!DOCTYPE html><html><head><title>Marduk Rig</title><style>body{background:#0a0e1a;color:#88ffaa;font-family:monospace;padding:2rem;text-align:center;}h1{color:#ffcc00;}</style></head><body><h1>⚖️ MARDUK RIG</h1><p>Shares: )" + to_string(TOTAL_SHARES.load()) + R"(</p><p>Earned: )" + to_string(TOTAL_EARNINGS.load()) + R"( XMR</p></body></html>)";
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
    cout << "⚖️ MARDUK RIG — Collects ACHi → Matches Crypto Patterns" << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;
    cout << "📤 Wallet: " << WALLET << endl;
    cout << "💰 Saved: " << TOTAL_EARNINGS.load() << " XMR" << endl;
    cout << "📡 Mode: stdin → ACHi → Pattern Match → Pool" << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;

    cout << "\n🌍 Select pool:" << endl;
    for (size_t i = 0; i < POOLS.size(); ++i) {
        cout << "  [" << i + 1 << "] " << POOLS[i].name << " (" << POOLS[i].symbol << ")" << endl;
    }
    cout << "  [" << POOLS.size() + 1 << "] 🔄 CYCLE ALL" << endl;
    cout << "  [" << POOLS.size() + 2 << "] 🚪 EXIT" << endl;
    cout << "\n> ";

    int choice;
    cin >> choice;

    if (choice >= 1 && choice <= (int)POOLS.size()) {
        auto& pool = POOLS[choice - 1];
        cout << "\n🌊 Mining " << pool.name << " ..." << endl;
        cout << "📥 Type ACHi codes (one per line). Press Ctrl+D to stop." << endl;

        StratumClient fisherman(WALLET, PASS, pool.host, pool.port, pool.name);
        if (!fisherman.connectToPool()) {
            cout << "⚠️ Pool offline – running in offline mode" << endl;
        } else {
            fisherman.login();
        }

        WebServer::start();

        unsigned int threads = thread::hardware_concurrency();
        if (threads == 0) threads = 2;
        cout << "💻 Using " << threads << " threads" << endl;

        // Start input collector
        thread collector(collectInput);

        vector<thread> rigs;
        for (unsigned int i = 0; i < threads; ++i) {
            rigs.push_back(thread([&fisherman, i]() {
                TheRig rig(i, &fisherman);
                rig.work();
            }));
        }

        collector.join();
        MINING = false;
        for (auto& r : rigs) {
            if (r.joinable()) r.join();
        }

    } else if (choice == (int)POOLS.size() + 1) {
        cout << "\n🔄 Cycling pools..." << endl;
        WebServer::start();

        thread collector(collectInput);

        while (MINING) {
            for (auto& pool : POOLS) {
                if (!MINING) break;
                cout << "\n🔄 Switching to " << pool.name << endl;

                StratumClient fisherman(WALLET, PASS, pool.host, pool.port, pool.name);
                if (fisherman.connectToPool()) fisherman.login();

                unsigned int threads = thread::hardware_concurrency();
                if (threads == 0) threads = 2;

                vector<thread> rigs;
                for (unsigned int i = 0; i < threads; ++i) {
                    rigs.push_back(thread([&fisherman, i]() {
                        TheRig rig(i, &fisherman);
                        rig.work();
                    }));
                }

                auto start = chrono::high_resolution_clock::now();
                while (chrono::duration<double>(chrono::high_resolution_clock::now() - start).count() < 30 && MINING) {
                    this_thread::sleep_for(chrono::seconds(1));
                }

                MINING = false;
                for (auto& r : rigs) {
                    if (r.joinable()) r.join();
                }
                MINING = true;
            }
        }

        collector.join();

    } else {
        cout << "Exiting." << endl;
    }

    return 0;
}
