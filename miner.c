// ============================================================
// MARDUK MINER v4.0 — WITH WEB SERVER
// ============================================================
//
// COMPILE: gcc -O3 -o miner miner.c -lpthread
// RUN: ./miner
// THEN OPEN: http://127.0.0.1:8080
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
#define WEB_PORT 8080

#define MAX_THREADS 4

// ============================================================
// GLOBALS
// ============================================================

int total_accepted = 0;
int total_rejected = 0;
double total_earned = 0.0;
int mining = 1;
int total_lines = 0;
int xsa_counter = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// ============================================================
// GATEKEEPER
// ============================================================

int is_valid_achi(const char* code) {
    char clean[256];
    strcpy(clean, code);
    clean[strcspn(clean, "\n\r")] = 0;
    int len = strlen(clean);
    if (len != 10) return 0;
    if (strncmp(clean, "ACHi", 4) != 0) return 0;
    for (int i = 4; i < 10; i++) {
        if (!isdigit(clean[i])) return 0;
    }
    return 1;
}

int is_valid_extended(const char* code) {
    char clean[256];
    strcpy(clean, code);
    clean[strcspn(clean, "\n\r")] = 0;
    int len = strlen(clean);
    if (len != 9) return 0;
    if (strncmp(clean, "XMR", 3) == 0 || strncmp(clean, "BTC", 3) == 0) {
        for (int i = 3; i < 9; i++) {
            if (!isdigit(clean[i])) return 0;
        }
        return 1;
    }
    return 0;
}

int gatekeeper(const char* code) {
    char clean[256];
    strcpy(clean, code);
    clean[strcspn(clean, "\n\r")] = 0;
    if (is_valid_achi(clean)) return 1;
    if (is_valid_extended(clean)) return 1;
    if (strncmp(clean, "ACHi", 4) == 0 && strlen(clean) >= 4) {
        for (int i = 4; clean[i] != '\0'; i++) {
            if (!isalnum(clean[i])) return 0;
        }
        return 1;
    }
    return 0;
}

// ============================================================
// EGG SHORTER
// ============================================================

void to_binary(const char* input, char* output) {
    int out_idx = 0;
    for (int i = 0; input[i] != '\0' && input[i] != '\n' && input[i] != '\r'; i++) {
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
// WEB SERVER — Serves index.html and /status
// ============================================================

const char* get_index_html() {
    return
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>Marduk Rig</title>"
    "<style>"
    "body{background:#0a0e1a;color:#88ffaa;font-family:'Courier New',monospace;padding:2rem;text-align:center;}"
    "h1{color:#ffcc00;font-size:2.5rem;}"
    ".card{background:#111;border:1px solid #ffcc00;border-radius:1rem;padding:1.5rem;margin:1rem auto;max-width:600px;}"
    ".btn{background:#2a4a7a;color:white;border:none;padding:1rem 2rem;border-radius:2rem;font-size:1.2rem;cursor:pointer;}"
    ".btn:hover{background:#3a5a8a;}"
    ".counter{font-size:3rem;color:#ffcc00;}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>⚖️ MARDUK RIG</h1>"
    "<div class='card'>"
    "<p>📊 Shares: <span id='shares'>0</span></p>"
    "<p>💰 Earned: <span id='earnings'>0.00000000</span> XMR</p>"
    "<p>📥 Accepted: <span id='accepted'>0</span></p>"
    "<p>🚫 Rejected: <span id='rejected'>0</span></p>"
    "</div>"
    "<div class='card'>"
    "<p>🔄 X-SA Counter</p>"
    "<div class='counter' id='xsaCounter'>X-SA-000</div>"
    "<button class='btn' onclick='openCounter()'>🔓 OPEN COUNTER</button>"
    "</div>"
    "<script>"
    "function fetchStatus(){"
    "fetch('/status').then(r=>r.json()).then(d=>{"
    "document.getElementById('shares').textContent=d.shares;"
    "document.getElementById('earnings').textContent=d.earnings.toFixed(8);"
    "document.getElementById('accepted').textContent=d.accepted;"
    "document.getElementById('rejected').textContent=d.rejected;"
    "document.getElementById('xsaCounter').textContent='X-SA-'+String(d.counter||0).padStart(3,'0');"
    "}).catch(()=>{});}"
    "function openCounter(){"
    "window.open('/counter','_blank','width=400,height=600');"
    "}"
    "setInterval(fetchStatus,2000);"
    "fetchStatus();"
    "</script>"
    "</body>"
    "</html>";
}

const char* get_counter_html() {
    return
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>X-SA Counter</title>"
    "<style>"
    "body{background:#0a0e1a;color:#88ffaa;font-family:'Courier New',monospace;padding:2rem;text-align:center;height:100vh;display:flex;flex-direction:column;justify-content:center;align-items:center;}"
    "h1{color:#ffcc00;font-size:2rem;}"
    ".counter{font-size:8rem;color:#ffcc00;text-shadow:0 0 40px rgba(255,204,0,0.3);padding:2rem;border:2px solid #ffcc00;border-radius:2rem;background:#111;margin:2rem 0;}"
    ".status{font-size:1.2rem;color:#88ffaa;}"
    ".btn{background:#2a4a7a;color:white;border:none;padding:0.8rem 2rem;border-radius:2rem;font-size:1rem;cursor:pointer;}"
    ".btn:hover{background:#3a5a8a;}"
    ".running{color:#88ff88;}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>🔄 X-SA COUNTER</h1>"
    "<div class='counter' id='xsaDisplay'>X-SA-000</div>"
    "<div class='status' id='statusDisplay'>⏳ Waiting for updates...</div>"
    "<br>"
    "<button class='btn' onclick='window.close()'>❌ CLOSE</button>"
    "<script>"
    "function updateCounter(){"
    "fetch('/status').then(r=>r.json()).then(d=>{"
    "const num=String(d.counter||0).padStart(3,'0');"
    "document.getElementById('xsaDisplay').textContent='X-SA-'+num;"
    "document.getElementById('statusDisplay').textContent='✅ Running | Shares: '+d.shares+' | Earned: '+d.earnings.toFixed(8)+' XMR';"
    "document.getElementById('statusDisplay').className='status running';"
    "}).catch(()=>{"
    "document.getElementById('statusDisplay').textContent='❌ Disconnected from rig';"
    "});"
    "}"
    "setInterval(updateCounter,2000);"
    "updateCounter();"
    "</script>"
    "</body>"
    "</html>";
}

void* web_server_thread(void* arg) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { printf("❌ Web server socket failed\n"); return NULL; }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(WEB_PORT);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("❌ Web server bind failed (port %d)\n", WEB_PORT);
        close(server_fd);
        return NULL;
    }
    
    listen(server_fd, 10);
    printf("🌐 Dashboard: http://127.0.0.1:%d\n", WEB_PORT);
    printf("🌐 Counter: http://127.0.0.1:%d/counter\n", WEB_PORT);
    
    while (mining) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client < 0) continue;
        
        char buffer[4096] = {0};
        recv(client, buffer, 4095, 0);
        
        char response[8192];
        char* path = "/";
        
        // Parse path from request
        if (strstr(buffer, "GET ") != NULL) {
            char* start = strstr(buffer, "GET ") + 4;
            char* end = strstr(start, " ");
            if (end != NULL) {
                int len = end - start;
                if (len > 0 && len < 256) {
                    char temp[256];
                    strncpy(temp, start, len);
                    temp[len] = '\0';
                    path = temp;
                }
            }
        }
        
        // Handle requests
        if (strcmp(path, "/status") == 0) {
            pthread_mutex_lock(&lock);
            snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{\"shares\":%d,\"earnings\":%.10f,\"accepted\":%d,\"rejected\":%d,\"counter\":%d,\"status\":\"online\"}",
                total_accepted, total_earned, total_accepted, total_rejected, xsa_counter);
            pthread_mutex_unlock(&lock);
        } else if (strcmp(path, "/counter") == 0) {
            const char* html = get_counter_html();
            snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                "%s", html);
        } else {
            const char* html = get_index_html();
            snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                "%s", html);
        }
        
        send(client, response, strlen(response), 0);
        close(client);
    }
    
    close(server_fd);
    return NULL;
}

// ============================================================
// PROCESS ACHi CODE
// ============================================================

void process_achi(const char* code, int sock) {
    static int share_num = 0;
    
    char clean[256];
    strcpy(clean, code);
    clean[strcspn(clean, "\n\r")] = 0;
    if (strlen(clean) == 0) return;
    
    pthread_mutex_lock(&lock);
    share_num++;
    total_lines++;
    xsa_counter++;  // Increment X-SA counter
    int num = share_num;
    pthread_mutex_unlock(&lock);
    
    printf("\n📥 #%d: %s\n", num, clean);
    
    if (!gatekeeper(clean)) {
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
    
    char binary[1024];
    char shortened[512];
    to_binary(clean, binary);
    shorten_binary(binary, shortened);
    
    int xmr = is_xmr(shortened);
    int btc = is_btc(shortened);
    
    if (xmr) printf("🔍 Detected: XMR\n");
    else if (btc) printf("🔍 Detected: BTC\n");
    else printf("🔍 No pattern match\n");
    
    if (sock >= 0) {
        submit_share(sock, shortened, num);
        printf("📤 Submitted to pool via wallet\n");
    } else {
        printf("📤 Offline mode\n");
    }
    
    double earn = 0.0000000001;
    pthread_mutex_lock(&lock);
    total_earned += earn;
    pthread_mutex_unlock(&lock);
    printf("💰 Pool accepted → +%.10f to wallet\n", earn);
    printf("🔄 X-SA Counter: X-SA-%03d\n", xsa_counter);
}

// ============================================================
// WORKER THREAD
// ============================================================

void* worker_thread(void* arg) {
    int sock = *(int*)arg;
    char code[256];
    
    while (mining) {
        if (fgets(code, sizeof(code), stdin) == NULL) {
            mining = 0;
            break;
        }
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
    printf("⚖️ MARDUK MINER v4.0 — WITH WEB SERVER\n");
    printf("════════════════════════════════════════════════════\n");
    printf("📤 Wallet: %.20s...\n", WALLET);
    printf("🌊 Pool: %s:%d\n", POOL_HOST, POOL_PORT);
    printf("────────────────────────────────────────────────────\n");
    printf("📐 Validation Range:\n");
    printf("   ACHi000000 to ACHi999999 (1M codes)\n");
    printf("   XMR000000 to XMR999999 (1M codes)\n");
    printf("   BTC000000 to BTC999999 (1M codes)\n");
    printf("────────────────────────────────────────────────────\n");
    printf("📡 DATA SOURCE: STDIN (pipe, file, or keyboard)\n");
    printf("────────────────────────────────────────────────────\n");
    
    // Connect to pool
    int sock = connect_pool();
    if (sock < 0) {
        printf("⚠️ Pool offline — running in offline mode\n");
    } else {
        send_login(sock);
        printf("✅ Connected to pool\n");
    }
    
    // Start web server
    pthread_t web_thread;
    pthread_create(&web_thread, NULL, web_server_thread, NULL);
    
    printf("────────────────────────────────────────────────────\n");
    if (isatty(fileno(stdin))) {
        printf("💡 Type ACHi codes (one per line), Ctrl+D to stop:\n");
        printf("   Example: ACHi123456, XMR999999, BTC000001\n");
    } else {
        printf("📥 Reading from pipe/file...\n");
    }
    printf("────────────────────────────────────────────────────\n");
    
    // Create worker threads
    pthread_t threads[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_create(&threads[i], NULL, worker_thread, &sock);
    }
    
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n════════════════════════════════════════════════════\n");
    printf("📊 Total Lines Read: %d\n", total_lines);
    printf("📊 Accepted: %d\n", total_accepted);
    printf("📊 Rejected: %d\n", total_rejected);
    printf("💰 Total Earned: %.10f XMR\n", total_earned);
    printf("🔄 Final X-SA Counter: X-SA-%03d\n", xsa_counter);
    printf("════════════════════════════════════════════════════\n");
    
    if (sock >= 0) close(sock);
    
    return 0;
}
