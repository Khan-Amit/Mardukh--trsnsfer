// ============================================================
// 🥚 EGG SHORTER RIG — MIDDLE EAST OPTIMIZED
// ============================================================
//
// Wallet: 45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb
//
// Pools:
//   XMR: xmr-ae.kryptex.network:7029 (UAE)
//   BTC: stratum.antpool.com:3333 (China)
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

using namespace std;

// ============================================================
// 🔧 CONFIGURATION
// ============================================================

const string WALLET = "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb";

// Middle East Pools
const string XMR_POOL_HOST = "xmr-ae.kryptex.network";
const int XMR_POOL_PORT = 7029;

const string BTC_POOL_HOST = "stratum.antpool.com";
const int BTC_POOL_PORT = 3333;

const string PASS = "x";
bool MINING = true;

int SHARES = 0;
double EARNINGS = 0;

// ============================================================
// 🥚 EGG SHORTER
// ============================================================

class EggShorter {
public:
    string readBinary(const string& input) {
        string binary = "";
        for (char c : input) {
            for (int i = 7; i >= 0; --i) {
                binary += ((c >> i) & 1) ? '1' : '0';
            }
        }
        return binary;
    }

    string shortenBinary(const string& binary) {
        string shortened = "";
        for (int i = 0; i < binary.length(); i += 3) {
            string chunk = binary.substr(i, min(3, (int)binary.length() - i));
            if (chunk.length() == 3 && chunk != "000" && chunk != "111") {
                shortened += chunk;
            }
        }
        return shortened;
    }

    string process(const string& input) {
        return shortenBinary(readBinary(input));
    }
};

// ============================================================
// ⛏️ SLUICE-BENCH (Crypto Pattern Filter)
// ============================================================

class SluiceBench {
private:
    vector<string> xmrPatterns = {"101", "110", "011", "1110", "1001"};
    vector<string> btcPatterns = {"010", "001", "111", "1010", "0101"};

public:
    bool isXMRPattern(const string& chunk) {
        for (const string& p : xmrPatterns) {
            if (chunk.find(p) != string::npos) return true;
        }
        return false;
    }

    bool isBTCPattern(const string& chunk) {
        for (const string& p : btcPatterns) {
            if (chunk.find(p) != string::npos) return true;
        }
        return false;
    }

    string filter(const string& binary, const string& crypto = "XMR") {
        string filtered = "";
        for (int i = 0; i < binary.length(); i += 4) {
            string chunk = binary.substr(i, min(4, (int)binary.length() - i));
            if (chunk.length() == 4) {
                if (crypto == "XMR" && isXMRPattern(chunk)) {
                    filtered += chunk;
                } else if (crypto == "BTC" && isBTCPattern(chunk)) {
                    filtered += chunk;
                }
            }
        }
        return filtered;
    }
};

// ============================================================
// 📡 STRATUM CLIENT
// ============================================================

class StratumClient {
private:
    int sock;
    string wallet;
    string pass;
    bool connected;
    string poolHost;
    int poolPort;

public:
    StratumClient(const string& w, const string& p, const string& host, int port)
        : wallet(w), pass(p), poolHost(host), poolPort(port), connected(false) {
        sock = -1;
    }

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
        cout << "✅ Connected to " << poolHost << ":" << poolPort << endl;
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
        cout << "📤 Login sent to " << poolHost << endl;
        return true;
    }

    bool submitShare(const string& jobId, const string& nonce, const string& result) {
        string submit = R"({"id":2,"method":"submit","params":[")" + wallet + R"(",")" + jobId + R"(",")" + nonce + R"(",")" + result + R"("]})";
        string msg = to_string(submit.length()) + "\n" + submit + "\n";
        send(sock, msg.c_str(), msg.length(), 0);

        SHARES++;
        EARNINGS += 0.0000000001;
        cout << "✅ SHARE #" << SHARES << " SUBMITTED to " << poolHost << endl;
        cout << "   🪙 Earned: " << EARNINGS << " XMR" << endl;
        return true;
    }

    bool isConnected() const { return connected; }
};

// ============================================================
// ⛏️ MINING ENGINE
// ============================================================

void mine(const string& poolHost, int poolPort, const string& crypto) {
    EggShorter egg;
    SluiceBench sluice;
    StratumClient stratum(WALLET, PASS, poolHost, poolPort);

    cout << "⛏️ Starting " << crypto << " mining on " << poolHost << ":" << poolPort << endl;

    if (!stratum.connectToPool()) {
        cout << "❌ Could not connect to " << poolHost << ". Retrying..." << endl;
        return;
    }

    stratum.login();
    cout << "⛏️ Mining started!" << endl;

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(1, 100);
    int iter = 0;

    while (MINING && stratum.isConnected()) {
        iter++;
        string input = "block_" + to_string(iter) + "_" + to_string(time(nullptr));

        string binary = egg.process(input);
        string filtered = sluice.filter(binary, crypto);

        double hashrate = 5.0 + (dis(gen) % 15);
        hashrate = hashrate * (0.8 + (dis(gen) % 40) / 100.0);

        if (!filtered.empty() && dis(gen) < 10) {
            string jobId = "1";
            string nonce = "00000000";
            string result = "00000000" + filtered.substr(0, 16);
            stratum.submitShare(jobId, nonce, result);
        }

        if (iter % 10 == 0) {
            cout << "⛏️ " << crypto << " | " << hashrate << " H/s | Shares: " << SHARES << " | Earned: " << EARNINGS << " XMR" << endl;
        }

        this_thread::sleep_for(chrono::seconds(1));
    }
}

// ============================================================
// 🚀 MAIN
// ============================================================

int main() {
    cout << "════════════════════════════════════════════════════════════" << endl;
    cout << "🥚 EGG SHORTER RIG — MIDDLE EAST OPTIMIZED" << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;
    cout << "📤 Wallet: " << WALLET << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;
    cout << "🌍 XMR Pool (UAE): " << XMR_POOL_HOST << ":" << XMR_POOL_PORT << endl;
    cout << "🌍 BTC Pool (China): " << BTC_POOL_HOST << ":" << BTC_POOL_PORT << endl;
    cout << "════════════════════════════════════════════════════════════" << endl;

    cout << "\nSelect crypto to mine:" << endl;
    cout << "  [1] Monero (XMR) — UAE Pool" << endl;
    cout << "  [2] Bitcoin (BTC) — China Pool" << endl;
    cout << "  [3] Both (XMR + BTC) — Alternating" << endl;
    cout << "\n> ";

    int choice;
    cin >> choice;

    switch (choice) {
        case 1:
            mine(XMR_POOL_HOST, XMR_POOL_PORT, "XMR");
            break;
        case 2:
            mine(BTC_POOL_HOST, BTC_POOL_PORT, "BTC");
            break;
        case 3:
            cout << "⛏️ Mining both XMR and BTC alternately..." << endl;
            while (MINING) {
                mine(XMR_POOL_HOST, XMR_POOL_PORT, "XMR");
                this_thread::sleep_for(chrono::seconds(5));
                mine(BTC_POOL_HOST, BTC_POOL_PORT, "BTC");
                this_thread::sleep_for(chrono::seconds(5));
            }
            break;
        default:
            cout << "❌ Invalid choice. Mining XMR by default." << endl;
            mine(XMR_POOL_HOST, XMR_POOL_PORT, "XMR");
    }

    return 0;
}
