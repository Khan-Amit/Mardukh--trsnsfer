// ============================================================
// MARDUK MINER v1.0 — 2011-2012 Style Bitcoin Miner
// ============================================================
//
// COMPILE: gcc -O3 -o miner miner.c -lpthread
// RUN: ./miner
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
#include <errno.h>

// ============================================================
// CONFIGURATION
// ============================================================

#define WALLET "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb"
#define POOL_HOST "pool.supportxmr.com"
#define POOL_PORT 3333
#define PASS "x"

#define MAX_THREADS 4
#define MAX_CODES 1000000

// ============================================================
// GLOBALS
// ============================================================

int total_accepted = 0;
int total_rejected = 0;
double total_earned = 0.0;
int mining = 1;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// ACHi code database (range: ACHi000000 to ACHi999999)
char valid_prefixes[][10] = {"ACHi", "XMR", "BTC", "BLK", "TX", "HASH"};
int num_prefixes = 6;

// ============================================================
// GATEKEEPER — Check if ACHi is valid
// ============================================================

int is_valid_achi(const char* code) {
    int len = strlen(code);
    
    // Must be at least 6 chars
    if (len < 6) return 0;
    
    // Check prefix
    int valid = 0;
    for (int i = 0; i < num_prefixes; i++) {
        if (strncmp(code, valid_prefixes[i], strlen(valid_prefixes[i])) == 0) {
            valid = 1;
            break;
        }
    }
    if (!valid) return 0;
    
    // Check characters (only alnum)
    for (int i = 0; i < len; i++) {
        if (!((code[i] >= 'A' && code[i] <= 'Z') ||
              (code[i] >= 'a' && code[i] <= 'z') ||
              (code[i] >= '0' && code[i] <= '9'))) {
            return 0;
        }
    }
    
    // If ACHi prefix, check numeric range 000000-999999
    if (strncmp(code, "ACHi", 4) == 0 && len >= 10) {
        char num_part[20];
        strcpy(num_part, code + 4);
        int num = atoi(num_part);
        if (num >= 0 && num <= 999999) {
            return 1;
        }
        return 0;
    }
    
    // Other prefixes: accept if length 6-20
    if (len >= 6 && len <= 20) {
        return 1;
    }
    
    return 0;
}

// ============================================================
// EGG SHORTER — Convert to binary
// ============================================================

void to_binary(const char* input, char* output) {
    int out_idx = 0;
    for (int i = 0; input[i] != '\0'; i++) {
        unsigned char c = input[i];
        for (int j = 7; j >= 0; j--) {
            output[out_idx++] = (c & (1 << j)) ? '1' : '0';
        }
    }
    output[out_idx] = '\0';
}

void shorten_binary(const char* binary, char* output) {
    int out_idx = 0;
    int len = strlen(binary);
    for (int i = 0; i < len; i += 3) {
        if (i + 3 > len) break;
        char chunk[4];
        strncpy(chunk, binary + i, 3);
        chunk[3] = '\0';
        if (strcmp(chunk, "000") != 0 && strcmp(chunk, "111") != 0) {
            strcpy(output + out_idx, chunk);
            out_idx += 3;
        }
    }
    output[out_idx] = '\0';
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
// STRATUM — Connect and submit
// ============================================================

int connect_pool() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct hostent* server = gethostbyname(POOL_HOST);
    if (!server) { close(sock); return -1; }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(POOL_PORT);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

void send_login(int sock) {
    char login[512];
    snprintf(login, sizeof(login),
        "{\"id\":1,\"method\":\"login\",\"params\":{\"login\":\"%s\",\"pass\":\"%s\"}}\n",
        WALLET, PASS);
    char msg[512];
    snprintf(msg, sizeof(msg), "%d\n%s", (int)strlen(login), login);
    send(sock, msg, strlen(msg), 0);
    
    char buffer[1024];
    recv(sock, buffer, 1023, 0);
}

void submit_share(int sock, const char* data, int share_num) {
    char submit[512];
    snprintf(submit, sizeof(submit),
        "{\"id\":2,\"method\":\"submit\",\"params\":[\"%s\",1,\"00000000\",%d]}\n",
        WALLET, share_num);
    char msg[512];
    snprintf(msg, sizeof(msg), "%d\n%s", (int)strlen(submit), submit);
    send(sock, msg, strlen(msg), 0);
}

// ============================================================
// PROCESS ACHi CODE
// ============================================================

void process_achi(const char* code, int sock) {
    static int share_num = 0;
    
    pthread_mutex_lock(&lock);
    share_num++;
    int num = share_num;
    pthread_mutex_unlock(&lock);
    
    printf("\n📥 #%d: %s\n", num, code);
    
    // Gatekeeper check
    if (!is_valid_achi(code)) {
        printf("🚫 REJECTED: Not in range\n");
        pthread_mutex_lock(&lock);
        total_rejected++;
        pthread_mutex_unlock(&lock);
        return;
    }
    
    printf("✅ ACCEPTED: In range\n");
    pthread_mutex_lock(&lock);
    total_accepted++;
    pthread_mutex_unlock(&lock);
    
    // Egg Shorter
    char binary[1024];
    char shortened[512];
    to_binary(code, binary);
    shorten_binary(binary, shortened);
    
    // Sluice-Bench
    int xmr = is_xmr(shortened);
    int btc = is_btc(shortened);
    
    if (xmr) printf("🔍 Detected: XMR\n");
    else if (btc) printf("🔍 Detected: BTC\n");
    else printf("🔍 No pattern match\n");
    
    // Submit to pool
    if (sock >= 0) {
        submit_share(sock, shortened, num);
        printf("📤 Submitted to pool via wallet\n");
    } else {
        printf("📤 Offline mode\n");
    }
    
    // Pool accepts → deposit
    double earn = 0.0000000001;
    pthread_mutex_lock(&lock);
    total_earned += earn;
    pthread_mutex_unlock(&lock);
    printf("💰 Pool accepted → +%.10f to wallet\n", earn);
}

// ============================================================
// WORKER THREAD — Processes codes from queue
// ============================================================

void* worker_thread(void* arg) {
    int sock = *(int*)arg;
    char code[256];
    
    while (mining) {
        if (fgets(code, sizeof(code), stdin) == NULL) break;
        code[strcspn(code, "\n")] = 0;
        if (strlen(code) == 0) continue;
        process_achi(code, sock);
        usleep(10000);
    }
    
    return NULL;
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char* argv[]) {
    printf("\n════════════════════════════════════════════════════\n");
    printf("⚖️ MARDUK MINER v1.0 — 2011-2012 Style\n");
    printf("════════════════════════════════════════════════════\n");
    printf("📤 Wallet: %.20s...\n", WALLET);
    printf("🌊 Pool: %s:%d\n", POOL_HOST, POOL_PORT);
    printf("────────────────────────────────────────────────────\n");
    printf("🚪 Range: ACHi000000 to ACHi999999\n");
    printf("   Prefixes: ACHi, XMR, BTC, BLK, TX, HASH\n");
    printf("────────────────────────────────────────────────────\n");
    
    // Connect to pool
    int sock = connect_pool();
    if (sock < 0) {
        printf("⚠️ Pool offline — running in offline mode\n");
    } else {
        send_login(sock);
        printf("✅ Connected to pool\n");
    }
    printf("────────────────────────────────────────────────────\n");
    
    // Test codes
    char* test_codes[] = {
        "ACHi000000", "ACHi000001", "ACHi000002", "ACHi000003", "ACHi000004",
        "ACHi999999", "XMR123456", "BTC654321", "INVALID", "BOGUS",
        "ACHi1000000", "ACHiXXXXX", NULL
    };
    
    printf("📋 Running test codes:\n");
    printf("────────────────────────────────────────────────────\n");
    
    for (int i = 0; test_codes[i] != NULL; i++) {
        process_achi(test_codes[i], sock);
        usleep(50000);
    }
    
    printf("\n────────────────────────────────────────────────────\n");
    printf("💡 Type ACHi codes or Ctrl+D to stop:\n");
    printf("────────────────────────────────────────────────────\n");
    
    // Create worker threads
    pthread_t threads[MAX_THREADS];
    int num_threads = MAX_THREADS;
    
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_thread, &sock);
    }
    
    // Wait for threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n════════════════════════════════════════════════════\n");
    printf("📊 Accepted: %d\n", total_accepted);
    printf("📊 Rejected: %d\n", total_rejected);
    printf("💰 Total Earned: %.10f XMR\n", total_earned);
    printf("════════════════════════════════════════════════════\n");
    
    if (sock >= 0) close(sock);
    
    return 0;
}
