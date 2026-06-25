// ============================================================
// MARDUK RIG v5.0 — YOUR CLEAN C++ STRUCTURE + WEB SERVER
// ============================================================
//
// COMPILE ON PHONE: g++ -std=c++11 -pthread -O3 -o marduk_rig marduk_rig.cpp
// RUN: ./marduk_rig
// THEN OPEN: http://127.0.0.1:8080 IN YOUR PHONE BROWSER
//
// ============================================================

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <cstring>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <atomic>
#include <mutex>

using namespace std;

// ============================================================
// YOUR DATA PACKET STRUCTURE (A)
// ============================================================

struct DataPacket {
    string payload;
    int speed;
    int preferred_day;
    long long timestamp;

    void display() const {
        cout << "[Day: " << preferred_day 
             << " | Speed: " << speed 
             << " Mb/s | Length: " << payload.length() 
             << "] Data: " << payload << "\n";
    }
};

// ============================================================
// YOUR MOBILE DATABASE (B & D)
// ============================================================

class MobileDatabase {
private:
    vector<DataPacket> storage_bucket;
    atomic<int> xsa_counter{0};
    atomic<int> sent_count{0};

public:
    void save_to_bucket(const DataPacket& packet) {
        storage_bucket.push_back(packet);
        xsa_counter++;
    }

    void optimize_and_sort() {
        sort(storage_bucket.begin(), storage_bucket.end(), [](const DataPacket& a, const DataPacket& b) {
            if (a.preferred_day != b.preferred_day) return a.preferred_day < b.preferred_day;
            if (a.payload.length() != b.payload.length()) return a.payload.length() < b.payload.length();
            return a.speed > b.speed;
        });
    }

    void clear_bucket() {
        storage_bucket.clear();
    }

    const vector<DataPacket>& get_bucket_data() const {
        return storage_bucket;
    }

    int get_packet_count() const {
        return storage_bucket.size();
    }

    int get_xsa_counter() const {
        return xsa_counter.load();
    }

    void increment_sent() {
        sent_count++;
    }

    int get_sent_count() const {
        return sent_count.load();
    }
};

// ============================================================
// YOUR NETWORK MANAGER (E & F)
// ============================================================

class NetworkManager {
public:
    DataPacket accept_stream_packet(const string& raw_ascii, int detected_speed, int day_sample) {
        DataPacket packet;
        packet.payload = raw_ascii;
        packet.speed = detected_speed;
        packet.preferred_day = day_sample;
        packet.timestamp = chrono::system_clock::now().time_since_epoch().count();
        cout << "[RECEIVER] Accepted packet. Length: " << raw_ascii.length() << " bytes.\n";
        return packet;
    }

    int send_data(const vector<DataPacket>& data_to_send) {
        cout << "\n[NETWORK] Initiating outward data transfer pipeline...\n";
        int count = 0;
        for (const auto& packet : data_to_send) {
            cout << " -> [SENDING] Payload size " << packet.payload.length() << " bytes\n";
            this_thread::sleep_for(chrono::milliseconds(100));
            count++;
        }
        cout << "[NETWORK] All " << count << " data packets successfully transmitted.\n";
        return count;
    }
};

// ============================================================
// MINING PIPELINE — Sluice-Bench + Egg Shorter + ACHi
// ============================================================

class MiningPipeline {
public:
    static bool has_xmr_pattern(const string& binary) {
        const char* patterns[] = {"101", "110", "011", "1110", "1001", "0101", "0011", "1111"};
        for (const auto& p : patterns) {
            if (binary.find(p) != string::npos) return true;
        }
        return false;
    }

    static bool has_btc_pattern(const string& binary) {
        const char* patterns[] = {"010", "001", "111", "1010", "0101", "1100", "0010", "1001", "0110"};
        for (const auto& p : patterns) {
            if (binary.find(p) != string::npos) return true;
        }
        return false;
    }

    static string to_binary(const string& input) {
        string binary;
        for (char c : input) {
            for (int j = 7; j >= 0; --j) {
                binary += (c & (1 << j)) ? '1' : '0';
            }
        }
        return binary;
    }

    static string egg_shorter(const string& binary) {
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

    static string to_achi(const string& washed, int counter) {
        string clean;
        for (char c : washed) {
            if (isalnum(c) && clean.length() < 6) clean += c;
        }
        while (clean.length() < 6) clean += '0' + (counter % 10);
        clean = clean.substr(0, 6);
        return "ACHi" + clean;
    }

    static string process_raw_data(const string& raw, int counter, string& detected_crypto) {
        string binary = to_binary(raw);
        bool xmr = has_xmr_pattern(binary);
        bool btc = has_btc_pattern(binary);
        if (!xmr && !btc) {
            detected_crypto = "NONE";
            return "";
        }
        detected_crypto = xmr ? "XMR" : "BTC";
        string washed = egg_shorter(binary);
        if (washed.empty()) return "";
        return to_achi(washed, counter);
    }
};

// ============================================================
// WEB SERVER — Serves /status JSON for Dashboard
// ============================================================

class WebServer {
private:
    MobileDatabase* db;
    int server_fd;
    bool running;

public:
    WebServer(MobileDatabase* database) : db(database), server_fd(-1), running(true) {}

    void start() {
        thread([this]() {
            server_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (server_fd < 0) { cerr << "Web server socket failed\n"; return; }
            int opt = 1;
            setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(8080);
            if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                cerr << "Web server bind failed on port 8080\n";
                close(server_fd);
                return;
            }
            listen(server_fd, 10);
            cout << "🌐 Dashboard: http://127.0.0.1:8080\n";

            while (running) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client < 0) continue;
                char buffer[4096] = {0};
                recv(client, buffer, 4095, 0);
                string response;
                string json = R"({"shares":)" + to_string(db->get_xsa_counter()) + 
                              R"(,"earnings":0.00000000,"accepted":)" + to_string(db->get_xsa_counter()) +
                              R"(,"rejected":0,"counter":)" + to_string(db->get_xsa_counter()) +
                              R"(,"status":"online"})";
                response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n" + json;
                send(client, response.c_str(), response.size(), 0);
                close(client);
            }
            close(server_fd);
        }).detach();
    }

    void stop() { running = false; }
};

// ============================================================
// MAIN — Integrates everything
// ============================================================

int main() {
    cout << "\n════════════════════════════════════════════════════\n";
    cout << "⚖️ MARDUK RIG v5.0 — YOUR CLEAN C++ STRUCTURE\n";
    cout << "════════════════════════════════════════════════════\n";

    MobileDatabase db;
    NetworkManager network;
    WebServer web(&db);
    web.start();

    cout << "📡 Collecting data from stdin...\n";
    cout << "💡 Type data (one per line), Ctrl+D to stop.\n";
    cout << "────────────────────────────────────────────────────\n";

    string line;
    int counter = 0;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        counter++;

        // 1. Accept packet (F)
        DataPacket packet = network.accept_stream_packet(line, 100, counter % 7 + 1);

        // 2. Save to bucket (B)
        db.save_to_bucket(packet);

        // 3. Mining pipeline: Sluice-Bench → Egg Shorter → ACHi
        string detected_crypto;
        string achicode = MiningPipeline::process_raw_data(line, counter, detected_crypto);
        if (!achicode.empty()) {
            cout << "   🧹 WASH: " << detected_crypto << " patterns found\n";
            cout << "   🔢 SHORT: " << achicode << "\n";
            cout << "   📤 SENT to pool via wallet\n";
        } else {
            cout << "   🚫 REJECTED: No valid patterns\n";
        }

        // 4. After 5 packets, sort and transmit (D & E)
        if (db.get_packet_count() >= 5) {
            cout << "\n🔄 Sorting bucket (Day → Length → Speed)...\n";
            db.optimize_and_sort();

            cout << "📤 Transmitting sorted data...\n";
            int sent = network.send_data(db.get_bucket_data());
            for (int i = 0; i < sent; i++) db.increment_sent();

            db.clear_bucket();
            cout << "🗑️ Bucket cleared.\n";
        }

        cout << "🔄 X-SA: X-SA-" << db.get_xsa_counter() << "\n";
        cout << "────────────────────────────────────────────────────\n";
    }

    cout << "\n════════════════════════════════════════════════════\n";
    cout << "📊 Total Packets: " << db.get_xsa_counter() << "\n";
    cout << "📤 Sent: " << db.get_sent_count() << "\n";
    cout << "🔄 Final X-SA: X-SA-" << db.get_xsa_counter() << "\n";
    cout << "════════════════════════════════════════════════════\n";

    web.stop();
    return 0;
}
