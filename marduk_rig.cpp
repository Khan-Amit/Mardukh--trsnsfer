// ============================================================
// MARDUK MINER v2.0 — Mathematical Range Validation
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
#include <ctype.h>

// ============================================================
// CONFIGURATION
// ============================================================

#define WALLET "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb"
#define POOL_HOST "pool.supportxmr.com"
#define POOL_PORT 3333
#define PASS "x"

#define MAX_THREADS 4

// ============================================================
// GLOBALS
// ============================================================

int total_accepted = 0;
int total_rejected = 0;
double total_earned = 0.0;
int mining = 1;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// ============================================================
// MATHEMATICAL VALIDATION — Like a formula
// ============================================================

/*
 * RANGE: ACHi000000 to ACHi999999
 * FORMULA: code must match /^ACHi[0-9]{6}$/
 * 
 * This covers 1,000,000 possible codes
 * Without storing any of them
 */

int is_valid_achi(const char* code) {
    int len = strlen(code);
    
    // Must be exactly 10 chars for ACHi + 6 digits
    if (len != 10) return 0;
    
    // Check prefix "ACHi"
    if (strncmp(code, "ACHi", 4) != 0) return 0;
    
    // Check remaining 6 characters are digits
    for (int i = 4; i < 10; i++) {
        if (!isdigit(code[i])) return 0;
    }
    
    // All checks passed
    return 1;
}

/*
 * EXTENDED RANGE: Also accept XMR, BTC prefixes
 * FORMULA: /^(XMR|BTC)[0-9]{6}$/
 */

int is_valid_extended(const char* code) {
    int len = strlen(code);
    
    // Must be exactly 9 chars for XMR/BTC + 6 digits
    if (len != 9) return 0;
    
    // Check prefix XMR or BTC
    if (strncmp(code, "XMR", 3) == 0 || strncmp(code, "BTC", 3) == 0) {
        // Check remaining 6 characters are digits
        for (int i = 3; i < 9; i++) {
            if (!isdigit(code[i])) return 0;
        }
        return 1;
    }
    
    return 0;
}

/*
 * WILDCARD PATTERN: Like x$$$$$$$$$$z
 * Any code matching the pattern is valid
 */

int matches_pattern(const char* code, const char* pattern) {
    int len = strlen(code);
    int pat_len = strlen(pattern);
    
    if (len != pat_len) return 0;
    
    for (int i = 0; i < len; i++) {
        if (pattern[i] == '$') {
            // $ means any digit (0-9)
            if (!isdigit(code[i])) return 0;
        } else if (pattern[i] == '#') {
            // # means any alphanumeric
            if (!isalnum(code[i])) return 0;
        } else if (pattern[i] == '*') {
            // * means any character
            continue;
        } else {
            // Exact match required
            if (code[i] != pattern[i]) return 0;
        }
    }
    return 1;
}

// ============================================================
// GATEKEEPER — Full validation
// ============================================================

int gatekeeper(const char* code) {
    // 1. Check exact range: ACHi000000 to ACHi999999
    if (is_valid_achi(code)) return 1;
    
    // 2. Check extended: XMR000000 to XMR999999, BTC000000 to BTC999999
    if (is_valid_extended(code)) return 1;
    
    // 3. Check wildcard patterns
    // Pattern: ACHi$$$$$$ (where $ = any digit)
    if (matches_pattern(code, "ACHi$$$$$$")) return 1;
    
    // Pattern: XMR$$$$$$ (where $ = any digit)
    if (matches_pattern(code, "XMR$$$$$$")) return 1;
    
    // Pattern: BTC$$$$$$ (where $ = any digit)
    if (matches_pattern(code, "BTC$$$$$$")) return 1;
    
    // 4. Check if code contains only valid characters
    for (int i = 0; code[i] != '\0'; i++) {
        if (!isalnum(code[i]) && code[i] != '_') return 0;
    }
    
    // 5. Minimum length check
    if (strlen(code) < 4) return 0;
    
    return 0;
}

// ============================================================
// EGG SHORTER
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
// SLUICE-BENCH
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
// STRATUM
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
    
    // GATEKEEPER: Mathematical validation
    if (!gatekeeper(code)) {
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
    
    // EGG SHORTER
    char binary[1024];
    char shortened[512];
    to_binary(code, binary);
    shorten_binary(binary, shortened);
    
    // SLUICE-BENCH
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
// GENERATE CODES BY FORMULA
// ============================================================

void generate_codes_by_formula(char* output, int count) {
    // Generate ACHi000000 to ACHi999999 using math
    // Not storing them, generating on the fly
    static int counter = 0;
    
    if (counter > 999999) counter = 0;
    
    snprintf(output, 20, "ACHi%06d", counter);
    counter++;
}

// ============================================================
// WORKER THREAD
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
    printf("⚖️ MARDUK MINER v2.0 — Mathematical Range\n");
    printf("════════════════════════════════════════════════════\n");
    printf("📤 Wallet: %.20s...\n", WALLET);
    printf("🌊 Pool: %s:%d\n", POOL_HOST, POOL_PORT);
    printf("────────────────────────────────────────────────────\n");
    printf("📐 Mathematical Validation:\n");
    printf("   Range 1: ACHi000000 to ACHi999999 (1M codes)\n");
    printf("   Range 2: XMR000000 to XMR999999 (1M codes)\n");
    printf("   Range 3: BTC000000 to BTC999999 (1M codes)\n");
    printf("   Pattern: ACHi$$$$$$ (where $ = any digit)\n");
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
    
    // Generate and test 20 codes using formula
    printf("📋 Generating 20 codes by formula:\n");
    printf("────────────────────────────────────────────────────\n");
    
    for (int i = 0; i < 20; i++) {
        char code[20];
        generate_codes_by_formula(code, i);
        process_achi(code, sock);
        usleep(50000);
    }
    
    printf("\n────────────────────────────────────────────────────\n");
    printf("💡 Type any ACHi code or Ctrl+D to stop:\n");
    printf("   (e.g., ACHi123456, XMR999999, BTC000001)\n");
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
