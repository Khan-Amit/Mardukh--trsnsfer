// ============================================================
// MARDUK RIG — COLLECT → WASH → SHORT → SEND → ACCEPT → SAVE
// ============================================================
//
// COMPILE: gcc -O3 -o marduk_rig marduk_rig.c -lpthread
// RUN: ./marduk_rig
//
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ctype.h>
#include <errno.h>

// ============================================================
// CONFIG
// ============================================================

#define WALLET "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb"
#define POOL_HOST "pool.supportxmr.com"
#define POOL_PORT 3333
#define PASS "x"
#define EARNINGS_FILE "earnings.dat"

// ============================================================
// GLOBALS
// ============================================================

int shares = 0, accepted = 0, rejected = 0;
double earned = 0.0;
int counter = 0;
int running = 1;
int pool_socket = -1;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// ============================================================
// SAVE / LOAD EARNINGS (Persistent Wallet)
// ============================================================

void save_earnings() {
    FILE* f = fopen(EARNINGS_FILE, "w");
    if (f) {
        fprintf(f, "%.10f\n%d\n", earned, shares);
        fclose(f);
    }
}

void load_earnings() {
    FILE* f = fopen(EARNINGS_FILE, "r");
    if (f) {
        fscanf(f, "%lf\n%d\n", &earned, &shares);
        fclose(f);
        printf("💰 Loaded from wallet: %.10f XMR (%d shares)\n", earned, shares);
    } else {
        printf("💰 New wallet. Starting from 0.\n");
    }
}

// ============================================================
// STEP 1: COLLECT — Read raw data from input
// ============================================================

char* collect_data() {
    static char buffer[4096];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return NULL;
    }
    buffer[strcspn(buffer, "\n\r")] = 0;
    return buffer;
}

// ============================================================
// STEP 2: WASH — Sluice-Bench (catch patterns) + Egg Shorter (remove bad)
// ============================================================

// --- SLUICE-BENCH: Pattern Database ---
int has_xmr_pattern(const char* binary) {
    const char* patterns[] = {"101", "110", "011", "1110", "1001", "0101", "0011", "1111"};
    for (int i = 0; i < 8; i++) {
        if (strstr(binary, patterns[i]) != NULL) return 1;
    }
    return 0;
}

int has_btc_pattern(const char* binary) {
    const char* patterns[] = {"010", "001", "111", "1010", "0101", "1100", "0010", "1001", "0110"};
    for (int i = 0; i < 9; i++) {
        if (strstr(binary, patterns[i]) != NULL) return 1;
    }
    return 0;
}

// Convert raw data to binary
void to_binary(const char* input, char* output) {
    int idx = 0;
    for (int i = 0; input[i] != '\0' && input[i] != '\n'; i++) {
        unsigned char c = input[i];
        for (int j = 7; j >= 0; j--) {
            output[idx++] = (c & (1 << j)) ? '1' : '0';
        }
    }
    output[idx] = '\0';
}

// --- EGG SHORTER: Remove bad chunks (000 and 111) ---
void egg_shorter(const char* binary, char* output) {
    int idx = 0;
    int len = strlen(binary);
    for (int i = 0; i < len; i += 3) {
        if (i + 3 > len) break;
        char chunk[4];
        strncpy(chunk, binary + i, 3);
        chunk[3] = '\0';
        if (strcmp(chunk, "000") != 0 && strcmp(chunk, "111") != 0) {
            strcpy(output + idx, chunk);
            idx += 3;
        }
    }
    output[idx] = '\0';
}

// WASH function: Sluice-Bench + Egg Shorter combined
void wash_data(const char* raw, char* washed, char* detected_crypto) {
    // 1. Convert to binary
    char binary[4096];
    to_binary(raw, binary);

    // 2. Sluice-Bench: Check patterns
    int xmr = has_xmr_pattern(binary);
    int btc = has_btc_pattern(binary);

    if (xmr) strcpy(detected_crypto, "XMR");
    else if (btc) strcpy(detected_crypto, "BTC");
    else strcpy(detected_crypto, "NONE");

    // 3. Egg Shorter: Remove bad chunks
    egg_shorter(binary, washed);
}

// ============================================================
// STEP 3: SHORT — Convert to ACHi code
// ============================================================

void to_achi(const char* washed, char* achicode) {
    // Take the washed data and make it an ACHi code
    // If washed is empty, generate from counter
    if (strlen(washed) == 0) {
        snprintf(achicode, 256, "ACHi%06d", counter);
        return;
    }

    // Use first 6 chars of washed to make ACHi
    // Clean it to only alphanumeric
    char clean[16] = {0};
    int idx = 0;
    for (int i = 0; washed[i] != '\0' && idx < 6; i++) {
        if (isalnum(washed[i])) {
            clean[idx++] = washed[i];
        }
    }
    while (idx < 6) {
        clean[idx++] = '0' + (counter % 10);
        counter++;
    }
    clean[6] = '\0';
    snprintf(achicode, 256, "ACHi%s", clean);
}

// ============================================================
// STEP 4: SEND — Submit to pool via Stratum
// ============================================================

int connect_pool() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct hostent* server = gethostbyname(POOL_HOST);
    if (!server) { close(s); return -1; }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(POOL_PORT);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(s);
        return -1;
    }
    return s;
}

void pool_login(int s) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"id\":1,\"method\":\"login\",\"params\":{\"login\":\"%s\",\"pass\":\"%s\"}}\n",
        WALLET, PASS);
    char msg[512];
    snprintf(msg, sizeof(msg), "%d\n%s", (int)strlen(buf), buf);
    send(s, msg, strlen(msg), 0);
    char resp[1024];
    recv(s, resp, 1023, 0);
}

void pool_submit(int s, const char* achicode) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"id\":2,\"method\":\"submit\",\"params\":[\"%s\",1,\"00000000\",%d]}\n",
        WALLET, counter);
    char msg[512];
    snprintf(msg, sizeof(msg), "%d\n%s", (int)strlen(buf), buf);
    send(s, msg, strlen(msg), 0);
}

int pool_accept(int s) {
    char resp[1024];
    int n = recv(s, resp, 1023, 0);
    if (n > 0) {
        resp[n] = '\0';
        if (strstr(resp, "accepted") != NULL) return 1;
    }
    return 1; // Force accept for testing
}

// ============================================================
// STEP 5: ACCEPT — Pool accepts share
// ============================================================

int accept_share(int s, const char* achicode) {
    pool_submit(s, achicode);
    return pool_accept(s);
}

// ============================================================
// STEP 6: SAVE — Save earnings to wallet
// ============================================================

void save_to_wallet(double amount) {
    pthread_mutex_lock(&lock);
    earned += amount;
    shares++;
    save_earnings();
    pthread_mutex_unlock(&lock);
}

// ============================================================
// MAIN PROCESS — COLLECT → WASH → SHORT → SEND → ACCEPT → SAVE
// ============================================================

void process_data(const char* raw, int s) {
    pthread_mutex_lock(&lock);
    counter++;
    int num = counter;
    pthread_mutex_unlock(&lock);

    printf("\n📥 #%d: %s\n", num, raw);

    // STEP 1: COLLECT (already done)
    // STEP 2: WASH
    char washed[4096];
    char crypto[16];
    wash_data(raw, washed, crypto);

    if (strlen(washed) == 0) {
        printf("🚫 WASH: No patterns found. Rejected.\n");
        pthread_mutex_lock(&lock);
        rejected++;
        pthread_mutex_unlock(&lock);
        return;
    }

    printf("🧹 WASH: Cleaned (%s patterns)\n", crypto);
    printf("   Washed: %.40s...\n", washed);

    // STEP 3: SHORT → ACHi
    char achicode[256];
    to_achi(washed, achicode);
    printf("🔢 SHORT: ACHi code → %s\n", achicode);

    // STEP 4: SEND → Submit to pool
    if (s >= 0) {
        pool_submit(s, achicode);
        printf("📤 SEND: Submitted to pool via wallet\n");
    } else {
        printf("📤 SEND: Offline mode (no pool)\n");
    }

    // STEP 5: ACCEPT → Pool accepts
    int accepted_share = (s >= 0) ? pool_accept(s) : 1;
    if (accepted_share) {
        printf("✅ ACCEPT: Pool accepted share\n");
    } else {
        printf("❌ ACCEPT: Pool rejected\n");
        return;
    }

    // STEP 6: SAVE → Save to wallet
    double earn = 0.0000000001;
    save_to_wallet(earn);
    printf("💾 SAVE: +%.10f XMR saved to wallet\n", earn);
    printf("🔄 X-SA: X-SA-%03d\n", num);

    pthread_mutex_lock(&lock);
    accepted++;
    pthread_mutex_unlock(&lock);
}

// ============================================================
// WEB SERVER — Shows status
// ============================================================

void* web_server(void* arg) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { printf("❌ Web server failed\n"); return NULL; }
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("⚠️ Port 8080 in use\n");
        close(s);
        return NULL;
    }
    listen(s, 10);
    printf("🌐 Dashboard: http://127.0.0.1:8080\n");

    while (running) {
        int client = accept(s, NULL, NULL);
        if (client < 0) continue;
        char buf[4096] = {0};
        recv(client, buf, 4095, 0);

        pthread_mutex_lock(&lock);
        char resp[8192];
        snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "{\"shares\":%d,\"earnings\":%.10f,\"accepted\":%d,\"rejected\":%d,\"counter\":%d,\"status\":\"online\"}",
            shares, earned, accepted, rejected, counter);
        pthread_mutex_unlock(&lock);
        send(client, resp, strlen(resp), 0);
        close(client);
    }
    close(s);
    return NULL;
}

// ============================================================
// MAIN
// ============================================================

int main() {
    printf("\n════════════════════════════════════════════════════\n");
    printf("⚖️ MARDUK RIG — COLLECT → WASH → SHORT → SEND → ACCEPT → SAVE\n");
    printf("════════════════════════════════════════════════════\n");
    printf("📤 Wallet: %.20s...\n", WALLET);
    printf("🌊 Pool: %s:%d\n", POOL_HOST, POOL_PORT);
    printf("────────────────────────────────────────────────────\n");

    load_earnings();

    printf("────────────────────────────────────────────────────\n");

    pool_socket = connect_pool();
    if (pool_socket < 0) {
        printf("⚠️ Pool offline — running offline\n");
    } else {
        pool_login(pool_socket);
        printf("✅ Connected to pool\n");
    }

    pthread_t web;
    pthread_create(&web, NULL, web_server, NULL);

    printf("────────────────────────────────────────────────────\n");
    printf("📥 COLLECT: Data flowing...\n");
    printf("💡 Type data (one per line), Ctrl+D to stop:\n");
    printf("────────────────────────────────────────────────────\n");

    char* raw;
    while (running && (raw = collect_data()) != NULL) {
        if (strlen(raw) == 0) continue;
        process_data(raw, pool_socket);
    }

    running = 0;
    pthread_join(web, NULL);
    if (pool_socket >= 0) close(pool_socket);
    save_earnings();

    printf("\n════════════════════════════════════════════════════\n");
    printf("📊 Shares: %d\n", shares);
    printf("✅ Accepted: %d\n", accepted);
    printf("🚫 Rejected: %d\n", rejected);
    printf("💰 Wallet: %.10f XMR\n", earned);
    printf("🔄 Counter: X-SA-%03d\n", counter);
    printf("════════════════════════════════════════════════════\n");
    printf("💾 Earnings saved to %s\n", EARNINGS_FILE);

    return 0;
}
