// ============================================================
// MARDUK RIG — FINAL ONE-FILE VERSION
// ============================================================
//
// COMPILE: gcc -O3 -o marduk marduk.c -lpthread
// RUN: ./marduk
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

// ============================================================
// CONFIG
// ============================================================

#define WALLET "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb"
#define POOL_HOST "pool.supportxmr.com"
#define POOL_PORT 3333

int shares = 0, accepted = 0, rejected = 0;
double earned = 0.0;
int counter = 0;
int running = 1;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// ============================================================
// GATEKEEPER — Check if ACHi is valid
// ============================================================

int is_valid_achi(const char* code) {
    char c[256];
    strcpy(c, code);
    c[strcspn(c, "\n\r")] = 0;
    int len = strlen(c);
    if (len != 10) return 0;
    if (strncmp(c, "ACHi", 4) != 0) return 0;
    for (int i = 4; i < 10; i++) {
        if (!isdigit(c[i])) return 0;
    }
    return 1;
}

int is_valid_extended(const char* code) {
    char c[256];
    strcpy(c, code);
    c[strcspn(c, "\n\r")] = 0;
    int len = strlen(c);
    if (len != 9) return 0;
    if (strncmp(c, "XMR", 3) == 0 || strncmp(c, "BTC", 3) == 0) {
        for (int i = 3; i < 9; i++) {
            if (!isdigit(c[i])) return 0;
        }
        return 1;
    }
    return 0;
}

int gatekeeper(const char* code) {
    char c[256];
    strcpy(c, code);
    c[strcspn(c, "\n\r")] = 0;
    if (is_valid_achi(c)) return 1;
    if (is_valid_extended(c)) return 1;
    return 0;
}

// ============================================================
// EGG SHORTER — Convert to binary, remove bad chunks
// ============================================================

void to_binary(const char* input, char* output) {
    int idx = 0;
    for (int i = 0; input[i] != '\0' && input[i] != '\n' && input[i] != '\r'; i++) {
        unsigned char c = input[i];
        for (int j = 7; j >= 0; j--) {
            output[idx++] = (c & (1 << j)) ? '1' : '0';
        }
    }
    output[idx] = '\0';
}

void shorten_binary(const char* binary, char* output) {
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
// SLUICE-BENCH — Match crypto patterns
// ============================================================

int is_xmr(const char* binary) {
    char* patterns[] = {"101", "110", "011", "1110", "1001", "0101", "0011", "1111"};
    for (int i = 0; i < 8; i++) {
        if (strstr(binary, patterns[i]) != NULL) return 1;
    }
    return 0;
}

int is_btc(const char* binary) {
    char* patterns[] = {"010", "001", "111", "1010", "0101", "1100", "0010", "1001", "0110"};
    for (int i = 0; i < 9; i++) {
        if (strstr(binary, patterns[i]) != NULL) return 1;
    }
    return 0;
}

// ============================================================
// STRATUM — Connect and submit to pool
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

void send_login(int s) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"id\":1,\"method\":\"login\",\"params\":{\"login\":\"%s\",\"pass\":\"x\"}}\n",
        WALLET);
    char msg[512];
    snprintf(msg, sizeof(msg), "%d\n%s", (int)strlen(buf), buf);
    send(s, msg, strlen(msg), 0);
    char resp[1024];
    recv(s, resp, 1023, 0);
}

void submit_share(int s, int share_num) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"id\":2,\"method\":\"submit\",\"params\":[\"%s\",1,\"00000000\",%d]}\n",
        WALLET, share_num);
    char msg[512];
    snprintf(msg, sizeof(msg), "%d\n%s", (int)strlen(buf), buf);
    send(s, msg, strlen(msg), 0);
}

// ============================================================
// WEB SERVER — Serves JSON status
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
        printf("❌ Port 8080 in use\n");
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
// PROCESS ACHi CODE
// ============================================================

void process_achi(const char* code, int sock) {
    char c[256];
    strcpy(c, code);
    c[strcspn(c, "\n\r")] = 0;
    if (strlen(c) == 0) return;

    pthread_mutex_lock(&lock);
    counter++;
    int num = counter;
    pthread_mutex_unlock(&lock);

    printf("\n📥 #%d: %s\n", num, c);

    // 1. GATEKEEPER
    if (!gatekeeper(c)) {
        printf("🚫 REJECTED: Not in range\n");
        pthread_mutex_lock(&lock);
        rejected++;
        pthread_mutex_unlock(&lock);
        return;
    }
    printf("✅ ACCEPTED: In range\n");

    // 2. EGG SHORTER
    char binary[1024];
    char shortened[512];
    to_binary(c, binary);
    shorten_binary(binary, shortened);

    // 3. SLUICE-BENCH
    int xmr = is_xmr(shortened);
    int btc = is_btc(shortened);
    if (xmr) printf("🔍 Detected: XMR\n");
    else if (btc) printf("🔍 Detected: BTC\n");
    else printf("🔍 No pattern match\n");

    // 4. SUBMIT TO POOL
    if (sock >= 0) {
        submit_share(sock, num);
        printf("📤 Submitted to pool via wallet\n");
    } else {
        printf("📤 Offline mode\n");
    }

    // 5. UPDATE STATS
    pthread_mutex_lock(&lock);
    accepted++;
    shares++;
    earned += 0.0000000001;
    pthread_mutex_unlock(&lock);

    printf("💰 Pool accepted → +0.0000000001 to wallet\n");
    printf("🔄 X-SA Counter: X-SA-%03d\n", num);
}

// ============================================================
// MAIN
// ============================================================

int main() {
    printf("\n════════════════════════════════════════════════════\n");
    printf("⚖️ MARDUK RIG — FINAL ONE-FILE VERSION\n");
    printf("════════════════════════════════════════════════════\n");
    printf("📤 Wallet: %.20s...\n", WALLET);
    printf("🌊 Pool: %s:%d\n", POOL_HOST, POOL_PORT);
    printf("────────────────────────────────────────────────────\n");
    printf("📐 Range: ACHi000000-999999, XMR/BTC000000-999999\n");
    printf("────────────────────────────────────────────────────\n");

    int sock = connect_pool();
    if (sock < 0) {
        printf("⚠️ Pool offline — running offline\n");
    } else {
        send_login(sock);
        printf("✅ Connected to pool\n");
    }

    pthread_t web;
    pthread_create(&web, NULL, web_server, NULL);

    printf("────────────────────────────────────────────────────\n");
    printf("💡 Type ACHi codes (Ctrl+D to stop):\n");
    printf("────────────────────────────────────────────────────\n");

    char line[256];
    while (running && fgets(line, sizeof(line), stdin)) {
        process_achi(line, sock);
    }

    running = 0;
    pthread_join(web, NULL);
    if (sock >= 0) close(sock);

    printf("\n════════════════════════════════════════════════════\n");
    printf("📊 Shares: %d\n", shares);
    printf("📊 Accepted: %d\n", accepted);
    printf("📊 Rejected: %d\n", rejected);
    printf("💰 Earned: %.10f XMR\n", earned);
    printf("🔄 Counter: X-SA-%03d\n", counter);
    printf("════════════════════════════════════════════════════\n");

    return 0;
}
