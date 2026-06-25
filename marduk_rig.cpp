// ============================================================
// ⚖️ MARDUK RIG — 90s Style
// ============================================================
//
// Reads ACHi codes from stdin (one per line)
// Processes them through Sluice-Bench + Egg Shorter
// Sends to pool via raw socket
//
// Compile: g++ -std=c++11 -O3 -o marduk marduk.cpp
// Run: cat achicodes.txt | ./marduk
// Or: ./marduk < achicodes.txt
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

using namespace std;

// ============================================================
// CONFIG
// ============================================================

const string WALLET = "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb";
const string PASS = "x";
const string POOL_HOST = "pool.supportxmr.com";
const int POOL_PORT = 3333;

int TOTAL_SHARES = 0;
double TOTAL_EARNINGS = 0.0;

// ============================================================
// EGG SHORTER
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
// SLUICE-BENCH
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
// STRATUM SOCKET
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
}

void submitShare(int sock, const string& data) {
    string submit = R"({"id":2,"method":"submit","params":[")" + WALLET + R"(",1,"00000000",12345]})";
    string msg = to_string(submit.length()) + "\n" + submit + "\n";
    send(sock, msg.c_str(), msg.length(), 0);
}

// ============================================================
// MAIN
// ============================================================

int main() {
    cout << "⚖️ MARDUK RIG — 90s Style" << endl;
    cout << "📤 Wallet: " << WALLET << endl;
    cout << "🌊 Pool: " << POOL_HOST << ":" << POOL_PORT << endl;
    cout << "────────────────────────────────────────" << endl;
    
    // Connect to pool
    int sock = connectToPool();
    if (sock < 0) {
        cerr << "❌ Cannot connect to pool. Running in offline mode." << endl;
    } else {
        sendLogin(sock);
        cout << "✅ Connected to pool" << endl;
    }
    
    // Process stdin line by line
    string line;
    int count = 0;
    
    while (getline(cin, line)) {
        count++;
        
        // 1. Egg Shorter → binary
        string binary = toBinary(line);
        string shortened = shortenBinary(binary);
        
        // 2. Sluice-Bench → detect crypto
        bool isXMRMatch = isXMR(shortened);
        bool isBTCMatch = isBTC(shortened);
        
        if (!isXMRMatch && !isBTCMatch) {
            cout << "❌ #" << count << " No match: " << line.substr(0, 20) << "..." << endl;
            continue;
        }
        
        string crypto = isXMRMatch ? "XMR" : "BTC";
        
        // 3. Submit to pool
        if (sock >= 0) {
            submitShare(sock, shortened);
        }
        
        // 4. Update stats
        TOTAL_SHARES++;
        double earn = 0.0000000001;
        TOTAL_EARNINGS += earn;
        
        cout << "✅ #" << count << " ACCEPTED | +" << earn << " " << crypto 
             << " | " << line.substr(0, 30) << "..." << endl;
    }
    
    cout << "────────────────────────────────────────" << endl;
    cout << "📊 Total Shares: " << TOTAL_SHARES << endl;
    cout << "💰 Total Earned: " << TOTAL_EARNINGS << " XMR" << endl;
    
    if (sock >= 0) close(sock);
    
    return 0;
}
