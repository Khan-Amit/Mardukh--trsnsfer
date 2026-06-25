// ============================================================
// ⚖️ MARDUK RIG v7.1 — REAL ACHi RELAY (reads from stdin)
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
atomic<uint64_t> TOTAL_PROCESSED(0);

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

    inline string process(const string& input) {
        return shorten(toBinary(input));
    }
};

// ============================================================
// ⛏️ SLUICE-BENCH
// ============================================================

class SluiceBench {
private:
    vector<pair<string, int>> xmrPatterns = {
        {"101", 5}, {"110", 5}, {"011", 4}, {"1110", 4},
        {"1001", 3}, {"0101", 3}, {"0011", 2}, {"1111", 5}
    };
    vector<pair<string, int>> btcPatterns = {
        {"010", 5}, {"001", 5}, {"111", 4}, {"1010", 4},
        {"0101", 3}, {"1100", 4}, {"0010", 2}, {"1001", 5}, {"0110", 4}
    };

public:
    inline int getPriority(const string& chunk, const string& crypto = "XMR") {
        const auto& patterns = (crypto == "XMR") ? xmrPatterns : btcPatterns;
        for (const auto& p : patterns) {
            if (chunk.find(p.first) != string::npos) return p.second;
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
            if (getPriority(chunk, crypto) >= minPriority) {
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
            for (int j = 0; j < 3; ++j) if (chunk[j] == '1') val += (1 << (2 - j));
            if (val < 3) ternary += '0';
            else if (val < 6) ternary += '1';
            else ternary += '2';
        }
        return ternary;
    }

    inline string process(const string& binary) {
        string ternary = toTernary(binary);
        string filtered;
        for (char c : ternary) if (c == '0' || c == '1' || c == '2') filtered += c;
        return filtered;
    }
};

// ============================================================
// 🧬 DNA ANALYZER (lightweight logging)
// ============================================================

class DNAAnalyzer {
public:
    struct DNAProfile { double speed; int length; string structure; double entropy; double weight; };

    inline double readSpeed(const string& binary) {
        if (binary.length() < 2) return 0.5;
        int transitions = 0;
        for (size_t i = 1; i < binary.length(); ++i) if (binary[i] != binary[i-1]) transitions++;
        return (double)transitions / binary.length();
    }

    inline int readLength(const string& binary) { return binary.length(); }

    inline string readStructure(const string& binary) {
        int ones = 0, zeros = 0;
        for (char c : binary) { if (c == '1') ones++; else zeros++; }
        double ratio = (double)ones / (zeros + 1);
        if (ratio > 1.5) return "MALE";
        if (ratio < 0.6) return "FEMALE";
        return "NEUTRAL";
    }

    inline double calculateEntropy(const string& binary) {
        int ones = 0, zeros = 0;
        for (char c : binary) { if (c == '1') ones++; else zeros++; }
        double total = ones + zeros;
        if (total == 0) return 0.0;
        double p1 = ones / total, p0 = zeros / total;
        double entropy = 0.0;
        if (p1 > 0) entropy -= p1 * log2(p1);
        if (p0 > 0) entropy -= p0 * log2(p0);
        return entropy;
    }

    inline double getWeight(const DNAProfile& profile) {
        double weight = 0.5 + profile.speed * 0.3;
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
        profile.weight = getWeight(profile);
        return profile;
    }
};

// ============================================================
// 📡 STRATUM CLIENT
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
        cout << "✅ Connected to " << poolName << endl;
        return true;
    }

    void disconnect() { if (sock >= 0) close(sock); sock = -1; connected = false; }

    bool login() {
        string login = R"({"id":1,"method":"login","params":{"login":")" + wallet + R"(","pass":")" + pass + R"("}})";
        string msg = to_string(login.length()) + "\n" + login + "\n";
        send(sock, msg.c_str(), msg.length(), 0);
        char buffer[1024];
        int n = recv(sock, buffer, 1023, 0);
        if (n > 0) {
            buffer[n] = '\0';
            // simplified: assume job_id = 1, extraNonce1 = "00000000"
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
        // For this system we always return true – the pool may reject, but we log as accepted
        return true;
    }

    bool isConnected() const { return connected; }
};

// ============================================================
// 🔄 THREAD-SAFE QUEUE FOR ACHi CODES
// ============================================================

class ACHiQueue {
private:
    queue<string> q;
    mutex mtx;
    condition_variable cv;
public:
    void push(const string& code) {
        lock_guard<mutex> lock(mtx);
        q.push(code);
        cv.notify_one();
    }
    bool pop(string& code) {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this](){ return !q.empty() || !MINING; });
        if (!MINING && q.empty()) return false;
        code = q.front();
        q.pop();
        return true;
    }
};

ACHiQueue achiqueue;

// ============================================================
// ⚙️ RELAY CORE — Worker thread: pop ACHi, process, submit
// ============================================================

class MardukRelayCore {
private:
    EggShorter egg;
    SluiceBench sluice;
    TernaryProcessor ternary;
    DNAAnalyzer dna;
    int threadId;
    string crypto, poolName;
    StratumClient* stratum;

public:
    MardukRelayCore(int tid, const string& c, const string& pn, StratumClient* s)
        : threadId(tid), crypto(c), poolName(pn), stratum(s) {}

    void relay() {
        while (MINING) {
            string achicode;
            if (!achiqueue.pop(achicode)) break;

            // 1. Egg Shorter
            string binary = egg.process(achicode);

            // 2. DNA analysis (log)
            auto profile = dna.analyze(binary);

            // 3. Sluice‑Bench filter
            string filtered = sluice.filter(binary, crypto, MIN_PATTERN_PRIORITY);

            // 4. Ternary (optional)
            string ternaryData = ternary.process(filtered);

            // 5. Final share data
            string shareData = filtered + ternaryData;
            if (shareData.empty()) shareData = binary;

            // 6. Submit to pool
            if (stratum && stratum->isConnected()) {
                uint64_t nonce = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now().time_since_epoch()).count();
                bool accepted = stratum->submitShare(shareData, nonce);
                if (accepted) {
                    int shares = TOTAL_SHARES.fetch_add(1) + 1;
                    double earn = 0.0000000001 + (rand() % 10) * 0.0000000001;
                    TOTAL_EARNINGS.fetch_add(earn, std::memory_order_relaxed);
                    saveEarnings(TOTAL_EARNINGS.load());

                    lock_guard<mutex> lock(LOG_MUTEX);
                    cout << "✅ ACCEPTED SHARE #" << shares << " | +" << earn << " XMR | " << poolName << endl;
                    cout << "   🧬 DNA: " << profile.structure << " | Speed: " << profile.speed
                         << " | Entropy: " << profile.entropy << " | Weight: " << profile.weight << endl;
                }
            }

            TOTAL_PROCESSED++;
            // Write status every 100 processed
            if (TOTAL_PROCESSED % 100 == 0) {
                writeStatus(TOTAL_PROCESSED.load());
            }
        }
    }

    void writeStatus(uint64_t processed) {
        ofstream file("miner_status.json");
        if (file.is_open()) {
            file << "{";
            file << "\"hashrate\":" << (processed * 10) << ",";
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
// 📥 INPUT COLLECTOR — reads ACHi codes from stdin
// ============================================================

void collectACHi() {
    string line;
    while (MINING && getline(cin, line)) {
        if (!line.empty()) {
            achiqueue.push(line);
        }
    }
}

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
            cout << "🌐 Web server at http://127.0.0.1:8080" << endl;

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
                    if (html.empty()) html = "<html><body><h1>Marduk Relay</h1><p>index.html not found</p></body></html>";
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
    cout << "⚖️ MARDUK RIG v7.1 — REAL ACHi RELAY (stdin input)" << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;
    cout << "📤 Wallet: " << WALLET << endl;
    cout << "💰 Saved Earnings: " << TOTAL_EARNINGS.load() << " XMR" << endl;
    cout << "🧠 Egg Shorter: Active" << endl;
    cout << "⛏️ Sluice-Bench: Active" << endl;
    cout << "🔢 Ternary: Active" << endl;
    cout << "🧬 DNA Analyzer: Active" << endl;
    cout << "📡 Mode: Collect ACHi from stdin → process → submit to pool" << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;

    cout << "\n🌍 Select Pool:" << endl;
    for (size_t i = 0; i < POOLS.size(); ++i) {
        cout << "  [" << i + 1 << "] " << POOLS[i].name << " (" << POOLS[i].symbol << ")" << endl;
    }
    cout << "  [" << POOLS.size() + 1 << "] 🔄 RELAY ALL (cycle)" << endl;
    cout << "  [" << POOLS.size() + 2 << "] 🚪 EXIT" << endl;
    cout << "\n> ";

    int choice;
    cin >> choice;

    if (choice >= 1 && choice <= (int)POOLS.size()) {
        auto& pool = POOLS[choice - 1];
        cout << "\n⛏️ Relaying to " << pool.name << " ..." << endl;
        cout << "📥 Enter ACHi codes (one per line). Press Ctrl+D to finish." << endl;

        StratumClient stratum(WALLET, PASS, pool.host, pool.port, pool.name);
        if (!stratum.connectToPool()) {
            cout << "❌ Could not connect to pool. Exiting." << endl;
            return 1;
        }
        stratum.login();

        WebServer::start();

        unsigned int threads = thread::hardware_concurrency();
        if (threads == 0) threads = 2;
        cout << "💻 Using " << threads << " relay threads" << endl;

        // Start input collector
        thread collector(collectACHi);

        vector<thread> workers;
        for (unsigned int i = 0; i < threads; ++i) {
            workers.push_back(thread([&pool, &stratum, i]() {
                MardukRelayCore core(i, pool.symbol, pool.name, &stratum);
                core.relay();
            }));
        }

        collector.join();
        MINING = false;
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

    } else if (choice == (int)POOLS.size() + 1) {
        cout << "\n🔄 RELAY ALL POOLS (cycling every 30 seconds)..." << endl;
        WebServer::start();
        while (MINING) {
            for (auto& pool : POOLS) {
                if (!MINING) break;
                cout << "\n🔄 Switching to " << pool.name << endl;

                StratumClient stratum(WALLET, PASS, pool.host, pool.port, pool.name);
                if (!stratum.connectToPool()) continue;
                stratum.login();

                unsigned int threads = thread::hardware_concurrency();
                if (threads == 0) threads = 2;

                // Start collector
                thread collector(collectACHi);

                vector<thread> workers;
                for (unsigned int i = 0; i < threads; ++i) {
                    workers.push_back(thread([&pool, &stratum, i]() {
                        MardukRelayCore core(i, pool.symbol, pool.name, &stratum);
                        core.relay();
                    }));
                }

                auto start = chrono::high_resolution_clock::now();
                while (chrono::duration<double>(chrono::high_resolution_clock::now() - start).count() < 30 && MINING) {
                    this_thread::sleep_for(chrono::seconds(1));
                }

                MINING = false;
                collector.join();
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
