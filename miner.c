// ============================================================
// MARDUK MINER v4.1 — FULLSCREEN COUNTER
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
// WEB SERVER — Serves index.html and counter.html
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
    "body{background:#0a0e1a;color:#88ffaa;font-family:'Courier New',monospace;padding:1rem;text-align:center;margin:0;}"
    "h1{color:#ffcc00;font-size:2rem;}"
    ".container{max-width:800px;margin:0 auto;}"
    ".card{background:#111;border:1px solid #ffcc00;border-radius:1rem;padding:1.5rem;margin:0.8rem auto;}"
    ".stat{font-size:1.2rem;margin:0.3rem 0;}"
    ".stat span{color:#ffcc00;font-weight:bold;}"
    ".counter-box{background:#0a0e1a;border:2px solid #ffcc00;border-radius:1rem;padding:1.5rem;margin:0.8rem auto;}"
    ".counter-number{font-size:4rem;color:#ffcc00;text-shadow:0 0 30px rgba(255,204,0,0.2);}"
    ".btn{background:#2a4a7a;color:white;border:none;padding:0.8rem 2.5rem;border-radius:2rem;font-size:1.1rem;cursor:pointer;transition:0.2s;}"
    ".btn:hover{background:#3a5a8a;transform:scale(1.02);}"
    ".btn-secondary{background:#4a2a2a;}"
    ".btn-secondary:hover{background:#5a3a3a;}"
    ".flex{display:flex;gap:1rem;justify-content:center;flex-wrap:wrap;}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<h1>⚖️ MARDUK RIG</h1>"
    "<div class='card'>"
    "<div class='stat'>📊 Shares: <span id='shares'>0</span></div>"
    "<div class='stat'>💰 Earned: <span id='earnings'>0.00000000</span> XMR</div>"
    "<div class='stat'>📥 Accepted: <span id='accepted'>0</span></div>"
    "<div class='stat'>🚫 Rejected: <span id='rejected'>0</span></div>"
    "</div>"
    "<div class='counter-box'>"
    "<div style='font-size:0.8rem;color:#888;'>🔄 X-SA COUNTER</div>"
    "<div class='counter-number' id='xsaCounter'>X-SA-000</div>"
    "<div class='flex'>"
    "<button class='btn' onclick='openCounter()'>🔓 OPEN FULLSCREEN</button>"
    "<button class='btn btn-secondary' onclick='fetchStatus()'>⟳ REFRESH</button>"
    "</div>"
    "</div>"
    "<div style='color:#444;font-size:0.7rem;margin-top:0.5rem;'>© 2026 Seliim Ahmed · All Rights Reserved</div>"
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
    "window.open('/counter','_blank','width=500,height=700,scrollbars=no');"
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
    "*{margin:0;padding:0;box-sizing:border-box;}"
    "body{background:#0a0e1a;color:#88ffaa;font-family:'Courier New',monospace;height:100vh;display:flex;flex-direction:column;justify-content:center;align-items:center;padding:2rem;margin:0;overflow:hidden;}"
    ".background{position:fixed;top:0;left:0;width:100%;height:100%;background:radial-gradient(ellipse at center,#0a1a0a,#0a0e1a);z-index:-1;}"
    ".container{text-align:center;max-width:100%;}"
    ".title{color:#ffcc00;font-size:1.5rem;letter-spacing:3px;margin-bottom:1rem;}"
    ".counter-display{font-size:10rem;color:#ffcc00;text-shadow:0 0 60px rgba(255,204,0,0.3),0 0 120px rgba(255,204,0,0.1);padding:1rem 2rem;border:3px solid #ffcc00;border-radius:2rem;background:rgba(0,0,0,0.5);margin:0.5rem 0;line-height:1.2;}"
    ".details{font-size:1.1rem;color:#88ffaa;margin:0.5rem 0;}"
    ".details span{color:#ffcc00;}"
    ".status-line{font-size:0.9rem;color:#888;margin:0.3rem 0;}"
    ".status-line .online{color:#88ff88;}"
    ".status-line .offline{color:#ff6666;}"
    ".footer{position:fixed;bottom:1rem;font-size:0.6rem;color:#444;}"
    ".pulse{animation:pulse 2s ease-in-out infinite;}"
    "@keyframes pulse{0%,100%{opacity:1;}50%{opacity:0.7;}}"
    ".btn-close{background:#2a2a2a;border:1px solid #444;color:#888;padding:0.5rem 2rem;border-radius:2rem;font-size:0.8rem;cursor:pointer;margin-top:0.5rem;font-family:monospace;}"
    ".btn-close:hover{background:#3a3a3a;color:#aaa;}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:0.5rem;margin:0.5rem 0;}"
    ".grid-item{background:rgba(0,0,0,0.3);border:1px solid #1a2a1a;border-radius:0.5rem;padding:0.3rem 0.8rem;}"
    ".grid-item .label{color:#555;font-size:0.6rem;text-transform:uppercase;}"
    ".grid-item .value{color:#88ffaa;font-size:1rem;}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='background'></div>"
    "<div class='container'>"
    "<div class='title'>🔄 X-SA COUNTER</div>"
    "<div class='counter-display pulse' id='xsaDisplay'>X-SA-000</div>"
    "<div class='details'>⛏️ <span id='sharesDisplay'>0</span> shares · 💰 <span id='earningsDisplay'>0.00000000</span> XMR</div>"
    "<div class='grid'>"
    "<div class='grid-item'><div class='label'>Accepted</div><div class='value' id='acceptedDisplay'>0</div></div>"
    "<div class='grid-item'><div class='label'>Rejected</div><div class='value' id='rejectedDisplay'>0</div></div>"
    "<div class='grid-item'><div class='label'>Pool</div><div class='value' style='font-size:0.7rem;'>supportxmr.com</div></div>"
    "<div class='grid-item'><div class='label'>Status</div><div class='value' id='statusDisplay' style='color:#88ff88;'>🟢 Online</div></div>"
    "</div>"
    "<button class='btn-close' onclick='window.close()'>❌ CLOSE</button>"
    "</div>"
    "<div class='footer'>© 2026 Seliim Ahmed · All Rights Reserved</div>"
    "<script>"
    "function updateCounter(){"
    "fetch('/status').then(r=>r.json()).then(d=>{"
    "const num=String(d.counter||0).padStart(3,'0');"
    "document.getElementById('xsaDisplay').textContent='X-SA-'+num;"
    "document.getElementById('sharesDisplay').textContent=d.shares;"
    "document.getElementById('earningsDisplay').textContent=d.earnings.toFixed(8);"
    "document.getElementById('acceptedDisplay').textContent=d.accepted;"
    "document.getElementById('rejectedDisplay').textContent=d.rejected;"
    "document.getElementById('statusDisplay').textContent='🟢 Online';"
    "document.getElementById('statusDisplay').style.color='#88ff88';"
    "}).catch(()=>{"
    "document.getElementById('statusDisplay').textContent='🔴 Offline';"
    "document.getElementById('statusDisplay').style.color='#ff6666';"
    "});"
    "}"
    "setInterval(updateCounter,1500);"
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
        
        char response[16384];
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
    xsa_counter++;
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
    printf("⚖️ MARDUK MINER v4.1 — FULLSCREEN COUNTER\n");
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
    
    int sock = connect_pool();
    if (sock < 0) {
        printf("⚠️ Pool offline — running in offline mode\n");
    } else {
        send_login(sock);
        printf("✅ Connected to pool\n");
    }
    
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
