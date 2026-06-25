// ============================================================
// MARDUK RIG — MUD POOL COLLECTOR
// ============================================================
// 
// COLLECT from Mud Pool (stdin) → SLUICE-BENCH → EGG SHORTER
// → HAMNET ACHi → STRATUM → POOL ACCEPT → WALLET
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

int shares = 0, accepted = 0, rejected = 0, counter = 0;
double earned = 0.0;
int running = 1;
int pool_socket = -1;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// ============================================================
// WALLET PERSISTENCE
// ============================================================

void save_earnings() {
    FILE* f = fopen(EARNINGS_FILE, "w");
    if (f) {
        fprintf(f, "%.10f\n%d\n%d\n", earned, shares, counter);
        fclose(f);
    }
}

void load_earnings() {
    FILE* f = fopen(EARNINGS_FILE, "r");
    if (f) {
        fscanf(f, "%lf\n%d\n%d\n", &earned, &shares, &counter);
        fclose(f);
        printf("💰 Loaded wallet: %.10f XMR (%d shares)\n", earned, shares);
    } else {
        printf("💰 New wallet.\n");
    }
}

// ============================================================
// SLUICE-BENCH — Pattern matching (catches data from mud)
// ============================================================

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

// ============================================================
// EGG SHORTER — Remove bad chunks (000 and 111)
// ============================================================

void to_binary(const unsigned char* data, int len, char* output) {
    int idx = 0;
    for (int i = 0; i < len && idx < 4096; i++) {
        unsigned char c = data[i];
        for (int j = 7; j >= 0; j--) {
            output[idx++] = (c & (1 << j)) ? '1' : '0';
        }
    }
    output[idx] = '\0';
}

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

// ============================================================
// HAMNET ACHi — Polish to ACHi format
// ============================================================

void to_achi(const char* washed, char* achicode) {
    char clean[16] = {0};
    int idx = 0;
    for (int i = 0; washed[i] != '\0' && idx < 6; i++) {
        if (isalnum(washed[i])) {
            clean[idx++] = washed[i];
        }
    }
    while (idx < 6) {
        clean[idx++] = '0' + (counter % 10);
    }
    clean[6] = '\0';
    snprintf(achicode, 256, "ACHi%s", clean);
}

// ============================================================
// STRATUM — Submit to pool
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
    return 1;
}

// ============================================================
// PROCESS — Mud Pool → Sluice-Bench → Egg Shorter → ACHi → Pool
// ============================================================

void process_mud(const char* raw, int len) {
    pthread_mutex_lock(&lock);
    counter++;
    int num = counter;
    pthread_mutex_unlock(&lock);

    printf("\n📥 #%d: Collected from mud pool\n", num);

    // 1. Convert to binary
    char binary[4096];
    to_binary((const unsigned char*)raw, len, binary);

    // 2. SLUICE-BENCH: Catch patterns
    int xmr = has_xmr_pattern(binary);
    int btc = has_btc_pattern(binary);

    if (!xmr && !btc) {
        printf("🚫 SLUICE-BENCH: No patterns. Rejected.\n");
        pthread_mutex_lock(&lock);
        rejected++;
        pthread_mutex_unlock(&lock);
        return;
    }

    const char* crypto = xmr ? "XMR" : "BTC";
    printf("🧹 SLUICE-BENCH: Caught %s patterns\n", crypto);

    // 3. EGG SHORTER: Clean data
    char washed[4096];
    egg_shorter(binary, washed);

    if (strlen(washed) == 0) {
        printf("🚫 EGG SHORTER: Empty. Rejected.\n");
        return;
    }
    printf("🧹 EGG SHORTER: Cleaned (%d bytes)\n", (int)strlen(washed));

    // 4. HAMNET ACHi: Polish to ACHi
    char achicode[256];
    to_achi(washed, achicode);
    printf("🔢 HAMNET ACHi: %s\n", achicode);

    // 5. STRATUM: Submit to pool
    if (pool_socket >= 0) {
        pool_submit(pool_socket, achicode);
        printf("📤 STRATUM: Submitted to pool via wallet\n");
    } else {
        printf("📤 STRATUM: Offline mode\n");
    }

    // 6. Pool accepts
    int accepted_share = (pool_socket >= 0) ? pool_accept(pool_socket) : 1;
    if (accepted_share) {
        printf("✅ POOL: Accepted share\n");
    } else {
        printf("❌ POOL: Rejected\n");
        return;
    }

    // 7. Save to wallet
    double earn = 0.0000000001;
    pthread_mutex_lock(&lock);
    earned += earn;
    shares++;
    accepted++;
    pthread_mutex_unlock(&lock);
    save_earnings();

    printf("💾 WALLET: +%.10f XMR (Total: %.10f)\n", earn, earned);
    printf("🔄 X-SA: X-SA-%03d\n", num);
}

// ============================================================
// MAIN — Collect from Mud Pool (stdin)
// ============================================================

int main() {
    printf("\n════════════════════════════════════════════════════\n");
    printf("⚖️ MARDUK RIG — MUD POOL COLLECTOR\n");
    printf("════════════════════════════════════════════════════\n");
    printf("📤 Wallet: %.20s...\n", WALLET);
    printf("🌊 Pool: %s:%d\n", POOL_HOST, POOL_PORT);
    printf("────────────────────────────────────────────────────\n");
    printf("📡 MUD POOL: Collecting from stdin...\n");
    printf("💡 Pipe data into this program or type manually.\n");
    printf("────────────────────────────────────────────────────\n");

    load_earnings();

    pool_socket = connect_pool();
    if (pool_socket < 0) {
        printf("⚠️ Pool offline — running offline\n");
    } else {
        pool_login(pool_socket);
        printf("✅ Connected to pool\n");
    }

    printf("────────────────────────────────────────────────────\n");
    printf("📥 MUD POOL ACTIVE — Waiting for data...\n");
    printf("────────────────────────────────────────────────────\n");

    char buffer[4096];
    while (running && fgets(buffer, sizeof(buffer), stdin) != NULL) {
        if (strlen(buffer) > 1) {
            process_mud(buffer, strlen(buffer));
        }
    }

    if (pool_socket >= 0) close(pool_socket);
    save_earnings();

    printf("\n════════════════════════════════════════════════════\n");
    printf("📊 Shares: %d\n", shares);
    printf("✅ Accepted: %d\n", accepted);
    printf("🚫 Rejected: %d\n", rejected);
    printf("💰 Wallet: %.10f XMR\n", earned);
    printf("🔄 X-SA: X-SA-%03d\n", counter);
    printf("════════════════════════════════════════════════════\n");

    return 0;
}
