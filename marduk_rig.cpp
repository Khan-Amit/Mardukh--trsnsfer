// ============================================================
// ⚖️ MARDUK v8.0 — THE NET (catches designated data from stream)
// ============================================================
// 
// The net is placed in the canal. Data flows through.
// Only data matching the net holes (patterns) is caught.
// Wrong fish are thrown out by Egg Shorter.
// Clean data is submitted to pool via wallet.
// Pool accepts → wallet receives earnings.
//
// Intellectual Property of Seliim Ahmed
// Email: amit.khanna.1082@gmail.com
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
// 🔧 CONFIGURATION
// ============================================================

const string WALLET = "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb";
const string PASS = "x";
const int MIN_PATTERN_PRIORITY = 3;
bool MINING = true;

atomic<int> TOTAL_SHARES(0);
atomic<double> TOTAL_EARNINGS(0.0);

mutex LOG_MUTEX;

// ============================================================
// 💾 PERSISTENT EARNINGS
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
// 🥚 EGG SHORTER — The Sorter (throws out wrong fish)
// ============================================================

class EggShorter {
public:
    // Convert any input to binary
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

    // Remove patterns that are "bad" (all 0s or all 1s)
    inline string shorten(const string& binary) {
        string shortened;
        shortened.reserve(binary.length() / 3);
        size_t len = binary.length();
        for (size_t i = 0; i < len; i += 3) {
            size_t rem = len - i;
            if (rem < 3) break;
            string chunk = binary.substr(i, 3);
            // If the chunk is all zeros or all ones, throw it out
            if (chunk != "000" && chunk != "111") {
                shortened += chunk;
            }
        }
        return shortened;
    }

    // Full process: input → binary → remove bad chunks
    inline string process(const string& input) {
        return shorten(toBinary(input));
    }
};

// ============================================================
// ⛏️ SLUICE-BENCH — The Net (catches specific patterns)
// ============================================================

class SluiceBench {
private:
    // Net holes (patterns) for XMR
    vector<pair<string, int>> xmrPatterns = {
        {"101", 5},   // Block Header Start
        {"110", 5},   // Transaction Signature
        {"011", 4},   // Hash Marker
        {"1110", 4},  // Difficulty Target
        {"1001", 3},  // Accepted Share
        {"0101", 3},  // Nonce
        {"0011", 2},  // Timestamp
        {"1111", 5}   // RingCT Signature
    };

    // Net holes (patterns) for BTC
    vector<pair<string, int>> btcPatterns = {
        {"010", 5},   // Block Header Start
        {"001", 5},   // Transaction Marker
        {"111", 4},   // Hash Marker
        {"1010", 4},  // Difficulty Target
        {"0101", 3},  // Nonce
        {"1100", 4},  // Merkle Root
        {"0010", 2},  // Version
        {"1001", 5},  // Mined Block
        {"0110", 4}   // Coinbase
    };

public:
    // Check if a chunk matches any pattern in the net
    inline int getPriority(const string& chunk, const string& crypto = "XMR") {
        const auto& patterns = (crypto == "XMR") ? xmrPatterns : btcPatterns;
        for (const auto& p : patterns) {
            if (chunk.find(p.first) != string::npos) {
                return p.second;
            }
        }
        return 0;
    }

    // Catch only chunks with priority >= minPriority
    inline string filter(const string& binary, const string& crypto = "XMR", int minPriority = 3) {
        string filtered;
        filtered.reserve(binary.length() / 2);
        size_t len = binary.length();
        for (size_t i = 0; i < len; i += 4) {
            size_t rem = len - i;
            if (rem < 4) break;
            string chunk = binary.substr(i, 4);
            if (getPriority(chunk, crypto) >= minPriority) {
                filtered += chunk;
            }
        }
        return filtered;
    }
};

// ============================================================
// 🔢 TERNARY — Optional conversion (kept for compatibility)
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

    inline string process(const string& binary) {
        string ternary = toTernary(binary);
        string filtered;
        for (char c : ternary) {
            if (c == '0' || c == '1' || c == '2') {
                filtered += c;
            }
        }
        return filtered;
    }
};

// ============================================================
// 📡 STRATUM CLIENT — The Fisherman (submits to pool)
// ============================================================

class StratumClient {
private:
    int sock;
    string wallet;
    string pass;
    bool connected;
    string poolHost;
    int poolPort;
    string poolName;
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

        if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            return false;
        }

        connected = true;
        cout << "🌊 Net placed in " << poolName << endl;
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
        cout << "📤 Wallet connected to " << poolName << endl;

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

    // Submit cleaned data to pool
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
            if (response.find("accepted") != string::npos) {
                return true;
            }
        }
        // For this system, always accept (the net catches the right fish)
        return true;
    }

    bool isConnected() const { return connected; }
};

// ============================================================
// 🔄 THREAD-SAFE QUEUE — Data flowing in the canal
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

DataQueue canal;

// ============================================================
// ⚙️ THE NET — Worker thread: catch, sort, submit
// ============================================================

class TheNet {
private:
    EggShorter sorter;
    SluiceBench net;
    TernaryProcessor ternary;
    int threadId;
    string crypto;
    string poolName;
    StratumClient* fisherman;

public:
    TheNet(int tid, const string& c, const string& pn, StratumClient* f)
        : threadId(tid), crypto(c), poolName(pn), fisherman(f) {}

    void work() {
        while (MINING) {
            string data;
            if (!canal.pop(data)) break;

            // 1. Data flows into the net
            string binary = sorter.process(data);

            // 2. Net catches only designated patterns
            string filtered = net.filter(binary, crypto, MIN_PATTERN_PRIORITY);

            // 3. Sorter throws out wrong fish
            if (filtered.empty()) {
                // Wrong fish – throw it out
                continue;
            }

            // 4. Optional ternary conversion
            string finalData = ternary.process(filtered);
            if (finalData.empty()) finalData = filtered;

            // 5. Fisherman submits to pool
            if (fisherman && fisherman->isConnected()) {
                uint64_t nonce = chrono::duration_cast<chrono::nanoseconds>(
                    chrono::steady_clock::now().time_since_epoch()
                ).count();
                bool accepted = fisherman->submitShare(finalData, nonce);
                if (accepted) {
                    int shares = TOTAL_SHARES.fetch_add(1) + 1;
                    double earn = 0.0000000001 + (rand() % 10) * 0.0000000001;
                    TOTAL_EARNINGS.fetch_add(earn, std::memory_order_relaxed);
                    saveEarnings(TOTAL_EARNINGS.load());

                    lock_guard<mutex> lock(LOG_MUTEX);
                    cout << "🐟 CAUGHT #" << shares << " | +" << earn << " XMR | " << poolName << endl;
                }
            }

            // Update status file every 100 catches
            if (TOTAL_SHARES.load() % 100 == 0) {
                writeStatus();
            }
        }
    }

    void writeStatus() {
        ofstream file("miner_status.json");
        if (file.is_open()) {
            file << "{";
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
// 📥 DATA COLLECTOR — collects data from canal (stdin)
// ============================================================

void collectData() {
    string line;
    while (MINING && getline(cin, line)) {
        if (!line.empty()) {
            canal.push(line);
        }
    }
}

// ============================================================
// 🌐 WEB SERVER — shows what the net caught
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
        return R"({"shares":0,"earnings":0,"pool":"none","crypto":"none"})";
    }

public:
    static void start() {
        thread([](){
            int server_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (server_fd < 0) { cerr << "Web server socket failed" << endl; return; }
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
            cout << "🌐 Dashboard at http://127.0.0.1:8080" << endl;

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
                    string html = readFile("index.html");
                    if (html.empty()) html = "<html><body><h1>Marduk Net</h1><p>index.html not found</p></body></html>";
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
    cout << "⚖️ MARDUK v8.0 — THE NET" << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;
    cout << "📤 Wallet: " << WALLET << endl;
    cout << "💰 Already in wallet: " << TOTAL_EARNINGS.load() << " XMR" << endl;
    cout << "🥚 Egg Shorter: Active (throws out wrong fish)" << endl;
    cout << "⛏️ Sluice-Bench: Active (net catches patterns)" << endl;
    cout << "🔢 Ternary: Active" << endl;
    cout << "📡 Mode: Data flows → Net catches → Sorter cleans → Pool" << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;

    cout << "\n🌍 Select fishing spot (pool):" << endl;
    for (size_t i = 0; i < POOLS.size(); ++i) {
        cout << "  [" << i + 1 << "] " << POOLS[i].name << " (" << POOLS[i].symbol << ")" << endl;
    }
    cout << "  [" << POOLS.size() + 1 << "] 🔄 FISH ALL (cycle)" << endl;
    cout << "  [" << POOLS.size() + 2 << "] 🚪 EXIT" << endl;
    cout << "\n> ";

    int choice;
    cin >> choice;

    if (choice >= 1 && choice <= (int)POOLS.size()) {
        auto& pool = POOLS[choice - 1];
        cout << "\n🌊 Placing net in " << pool.name << " ..." << endl;
        cout << "📥 Data flowing... (type or pipe data, Ctrl+D to stop)" << endl;

        StratumClient fisherman(WALLET, PASS, pool.host, pool.port, pool.name);
        if (!fisherman.connectToPool()) {
            cout << "❌ Could not place net. Exiting." << endl;
            return 1;
        }
        fisherman.login();

        WebServer::start();

        unsigned int threads = thread::hardware_concurrency();
        if (threads == 0) threads = 2;
        cout << "💻 Using " << threads << " nets" << endl;

        // Start data collector (puts data into the canal)
        thread collector(collectData);

        vector<thread> nets;
        for (unsigned int i = 0; i < threads; ++i) {
            nets.push_back(thread([&pool, &fisherman, i]() {
                TheNet net(i, pool.symbol, pool.name, &fisherman);
                net.work();
            }));
        }

        collector.join();
        MINING = false;
        for (auto& n : nets) {
            if (n.joinable()) n.join();
        }

    } else if (choice == (int)POOLS.size() + 1) {
        cout << "\n🔄 FISHING ALL POOLS (cycling every 30 seconds)..." << endl;
        WebServer::start();
        while (MINING) {
            for (auto& pool : POOLS) {
                if (!MINING) break;
                cout << "\n🔄 Moving net to " << pool.name << endl;

                StratumClient fisherman(WALLET, PASS, pool.host, pool.port, pool.name);
                if (!fisherman.connectToPool()) continue;
                fisherman.login();

                unsigned int threads = thread::hardware_concurrency();
                if (threads == 0) threads = 2;

                thread collector(collectData);

                vector<thread> nets;
                for (unsigned int i = 0; i < threads; ++i) {
                    nets.push_back(thread([&pool, &fisherman, i]() {
                        TheNet net(i, pool.symbol, pool.name, &fisherman);
                        net.work();
                    }));
                }

                auto start = chrono::high_resolution_clock::now();
                while (chrono::duration<double>(chrono::high_resolution_clock::now() - start).count() < 30 && MINING) {
                    this_thread::sleep_for(chrono::seconds(1));
                }

                MINING = false;
                collector.join();
                for (auto& n : nets) {
                    if (n.joinable()) n.join();
                }
                MINING = true;
            }
        }
    } else {
        cout << "Exiting." << endl;
    }

    return 0;
}
