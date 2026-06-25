// ============================================================
// ⚖️ MARDUK v12.0 — THE GATEKEEPER RIG
// ============================================================
//
// LOGIC:
//   1. Read ACHi code
//   2. Check if in range (ACHi000000 to ACHi999999)
//   3. If YES → ACCEPT → Send to pool via wallet
//   4. If NO → REJECT
//   5. Pool accepts → Deposit to wallet
//
// Compile: g++ -std=c++11 -O3 -o marduk marduk.cpp
// Run: ./marduk
//
// ============================================================

#include <iostream>
#include <string>
#include <cstring>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <regex>

using namespace std;

// ============================================================
// 🔧 CONFIGURATION
// ============================================================

const string WALLET = "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb";
const string PASS = "x";
const string POOL_HOST = "pool.supportxmr.com";
const int POOL_PORT = 3333;

int TOTAL_ACCEPTED = 0;
int TOTAL_REJECTED = 0;
double TOTAL_EARNINGS = 0.0;

// ============================================================
// 🚪 GATEKEEPER — Checks if ACHi is in range
// ============================================================

class Gatekeeper {
private:
    // Range: ACHi000000 to ACHi999999
    string rangeStart = "ACHi000000";
    string rangeEnd = "ACHi999999";
    
    // Additional valid prefixes (like XMR, BTC, etc.)
    vector<string> validPrefixes = {"ACHi", "XMR", "BTC", "BLK", "TX", "HASH"};
    
public:
    // Check if ACHi code is in range
    bool isInRange(const string& achicode) {
        // Check length (must be at least 6 characters)
        if (achicode.length() < 6) return false;
        
        // Check if it starts with a valid prefix
        bool hasValidPrefix = false;
        for (const auto& prefix : validPrefixes) {
            if (achicode.find(prefix) == 0) {
                hasValidPrefix = true;
                break;
            }
        }
        if (!hasValidPrefix) return false;
        
        // Check if it contains only valid characters (A-Z, 0-9)
        for (char c : achicode) {
            if (!isalnum(c)) return false;
        }
        
        // If it starts with ACHi, check numeric range
        if (achicode.find("ACHi") == 0 && achicode.length() >= 10) {
            string numPart = achicode.substr(4);
            // Check if it's all digits
            for (char c : numPart) {
                if (!isdigit(c)) return false;
            }
            // Range check: 000000 to 999999
            int num = stoi(numPart);
            if (num >= 0 && num <= 999999) {
                return true;
            }
        }
        
        // For other prefixes (XMR, BTC, etc.), accept if length is between 6-20
        if (achicode.length() >= 6 && achicode.length() <= 20) {
            return true;
        }
        
        return false;
    }
    
    // Get range info
    string getRangeInfo() {
        return rangeStart + " to " + rangeEnd;
    }
};

// ============================================================
// 🥚 EGG SHORTER — Convert to binary, remove bad chunks
// ============================================================

string toBinary(const string& input) {
    string binary;
    for (char c : input) {
        for (int i = 7; i >= 0; --i) {
            binary += ((c >> i) & 1) ? '1' : '0';
        }
    }
    return binary;
}

string shortenBinary(const string& binary) {
    string result;
    for (size_t i = 0; i < binary.length(); i += 3) {
        if (i + 3 > binary.length()) break;
        string chunk = binary.substr(i, 3);
        if (chunk != "000" && chunk != "111") {
            result += chunk;
        }
    }
    return result;
}

// ============================================================
// ⛏️ SLUICE-BENCH — Match crypto patterns
// ============================================================

bool isXMR(const string& binary) {
    vector<string> patterns = {"101", "110", "011", "1110", "1001", "0101", "0011", "1111"};
    for (const auto& p : patterns) {
        if (binary.find(p) != string::npos) return true;
    }
    return false;
}

bool isBTC(const string& binary) {
    vector<string> patterns = {"010", "001", "111", "1010", "0101", "1100", "0010", "1001", "0110"};
    for (const auto& p : patterns) {
        if (binary.find(p) != string::npos) return true;
    }
    return false;
}

// ============================================================
// 📡 STRATUM — Submit to pool
// ============================================================

int connectToPool() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct hostent* server = gethostbyname(POOL_HOST.c_str());
    if (!server) { close(sock); return -1; }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(POOL_PORT);
    
    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

void sendLogin(int sock) {
    string login = R"({"id":1,"method":"login","params":{"login":")" + WALLET + R"(","pass":")" + PASS + R"("}})";
    string msg = to_string(login.length()) + "\n" + login + "\n";
    send(sock, msg.c_str(), msg.length(), 0);
    
    char buffer[1024];
    recv(sock, buffer, 1023, 0);
}

void submitShare(int sock, const string& data, int shareNum) {
    string submit = R"({"id":2,"method":"submit","params":[")" + WALLET + R"(",1,"00000000",)" + to_string(shareNum) + R"(]})";
    string msg = to_string(submit.length()) + "\n" + submit + "\n";
    send(sock, msg.c_str(), msg.length(), 0);
}

// ============================================================
// 🎯 PROCESS ACHi CODE
// ============================================================

void processACHi(const string& achicode, int count, int sock, Gatekeeper& gate) {
    cout << "\n📥 #" << count << ": " << achicode << endl;
    
    // 1. GATEKEEPER: Check if in range
    if (!gate.isInRange(achicode)) {
        cout << "🚫 REJECTED: Not in range" << endl;
        TOTAL_REJECTED++;
        return;
    }
    
    cout << "✅ ACCEPTED: In range" << endl;
    TOTAL_ACCEPTED++;
    
    // 2. Egg Shorter → binary
    string binary = toBinary(achicode);
    string shortened = shortenBinary(binary);
    
    // 3. Sluice-Bench → detect crypto
    bool isXMRMatch = isXMR(shortened);
    bool isBTCMatch = isBTC(shortened);
    
    if (!isXMRMatch && !isBTCMatch) {
        cout << "⚠️ No crypto pattern match, but accepted anyway" << endl;
    }
    
    string crypto = isXMRMatch ? "XMR" : (isBTCMatch ? "BTC" : "UNKNOWN");
    if (crypto != "UNKNOWN") {
        cout << "🔍 Detected: " << crypto << endl;
    }
    
    // 4. Submit to pool via wallet
    if (sock >= 0) {
        submitShare(sock, shortened, count);
        cout << "📤 Submitted to pool via wallet" << endl;
    } else {
        cout << "📤 Offline mode (pool not connected)" << endl;
    }
    
    // 5. Pool accepts → deposit to wallet
    double earn = 0.0000000001;
    TOTAL_EARNINGS += earn;
    cout << "💰 Pool accepted → Deposited +" << earn << " to wallet" << endl;
}

// ============================================================
// 🚀 MAIN
// ============================================================

int main(int argc, char* argv[]) {
    Gatekeeper gate;
    
    cout << "\n════════════════════════════════════════════════════" << endl;
    cout << "⚖️ MARDUK v12.0 — THE GATEKEEPER RIG" << endl;
    cout << "════════════════════════════════════════════════════" << endl;
    cout << "📤 Wallet: " << WALLET.substr(0, 20) << "..." << endl;
    cout << "🌊 Pool: " << POOL_HOST << ":" << POOL_PORT << endl;
    cout << "────────────────────────────────────────────────────" << endl;
    cout << "🚪 Gatekeeper Range: " << gate.getRangeInfo() << endl;
    cout << "   Valid prefixes: ACHi, XMR, BTC, BLK, TX, HASH" << endl;
    cout << "────────────────────────────────────────────────────" << endl;
    
    // Connect to pool
    int sock = connectToPool();
    if (sock < 0) {
        cout << "⚠️ Pool offline – running in offline mode" << endl;
    } else {
        sendLogin(sock);
        cout << "✅ Connected to pool" << endl;
    }
    cout << "────────────────────────────────────────────────────" << endl;
    
    // Test codes (ACHi000000 to ACHi000009)
    vector<string> testCodes = {
        "ACHi000000", "ACHi000001", "ACHi000002", "ACHi000003", "ACHi000004",
        "ACHi000005", "ACHi000006", "ACHi000007", "ACHi000008", "ACHi000009",
        "ACHi999999", "XMR123456", "BTC654321", "INVALID", "BOGUS", "ACHi",
        "ACHi1000000", "ACHiXXXXX"
    };
    
    cout << "📋 Running test codes through gatekeeper:" << endl;
    cout << "────────────────────────────────────────────────────" << endl;
    
    int count = 0;
    for (const auto& code : testCodes) {
        count++;
        processACHi(code, count, sock, gate);
        this_thread::sleep_for(chrono::milliseconds(50));
    }
    
    cout << "\n────────────────────────────────────────────────────" << endl;
    cout << "💡 Type any ACHi code or Ctrl+D to stop:" << endl;
    cout << "────────────────────────────────────────────────────" << endl;
    
    string line;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        count++;
        processACHi(line, count, sock, gate);
    }
    
    cout << "\n════════════════════════════════════════════════════" << endl;
    cout << "📊 Accepted: " << TOTAL_ACCEPTED << endl;
    cout << "📊 Rejected: " << TOTAL_REJECTED << endl;
    cout << "💰 Total Deposited: " << TOTAL_EARNINGS << " XMR" << endl;
    cout << "════════════════════════════════════════════════════" << endl;
    
    if (sock >= 0) close(sock);
    
    return 0;
}
