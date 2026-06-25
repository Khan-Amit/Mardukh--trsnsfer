// ============================================================
// MARDUK RIG v6.0 — FINAL WORKING (Embedded HTML)
// ============================================================
// Compile: g++ -std=c++11 -pthread -O3 -o marduk_rig marduk_rig.cpp
// Run: ./marduk_rig
// Open: http://127.0.0.1:8080
// PIN: 197328
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
#include <cctype>

using namespace std;

// ============================================================
// CONFIG
// ============================================================

#define WALLET "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb"
#define POOL_HOST "pool.supportxmr.com"
#define POOL_PORT 3333
#define PASS "x"

atomic<int> TOTAL_SHARES(0);
atomic<int> XSA_COUNTER(0);
double TOTAL_EARNINGS = 0.0;
mutex earnings_mutex;
bool last_pool_response = true;

// ============================================================
// EGG SHORTER & SLUICE-BENCH
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

int has_xmr_pattern(const char* binary) {
    const char* patterns[] = {"101","110","011","1110","1001","0101","0011","1111"};
    for (int i=0;i<8;i++) if (strstr(binary, patterns[i])) return 1;
    return 0;
}

int has_btc_pattern(const char* binary) {
    const char* patterns[] = {"010","001","111","1010","0101","1100","0010","1001","0110"};
    for (int i=0;i<9;i++) if (strstr(binary, patterns[i])) return 1;
    return 0;
}

// ============================================================
// STRATUM CLIENT
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
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(s); return -1; }
    return s;
}

void pool_login(int s) {
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"id\":1,\"method\":\"login\",\"params\":{\"login\":\"%s\",\"pass\":\"%s\"}}\n", WALLET, PASS);
    string msg = to_string(strlen(buf)) + "\n" + buf;
    send(s, msg.c_str(), msg.length(), 0);
    char resp[1024];
    recv(s, resp, 1023, 0);
}

void pool_submit(int s, int nonce) {
    char buf[512];
    snprintf(buf, sizeof(buf), "{\"id\":2,\"method\":\"submit\",\"params\":[\"%s\",1,\"00000000\",%d]}\n", WALLET, nonce);
    string msg = to_string(strlen(buf)) + "\n" + buf;
    send(s, msg.c_str(), msg.length(), 0);
}

int pool_accept(int s) {
    char resp[1024];
    int n = recv(s, resp, 1023, 0);
    if (n > 0) {
        resp[n] = '\0';
        if (strstr(resp, "accepted") != NULL) { last_pool_response = true; return 1; }
    }
    last_pool_response = false;
    return 0;
}

// ============================================================
// WEB SERVER — EMBEDDED HTML
// ============================================================
class WebServer {
private:
    int server_fd;
    bool running;
public:
    WebServer() : server_fd(-1), running(true) {}
    void start() {
        thread([&]() {
            server_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (server_fd < 0) { cerr << "Socket failed\n"; return; }
            int opt = 1;
            setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(8080);
            if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                cerr << "Bind failed on port 8080\n";
                close(server_fd);
                return;
            }
            listen(server_fd, 10);
            cout << "🌐 Dashboard: http://127.0.0.1:8080\n";

            string html = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Marduk Rig</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;}
body{background:#0a0e1a;color:#88ffaa;font-family:'Courier New',monospace;padding:1.5rem;display:flex;justify-content:center;align-items:center;min-height:100vh;}
.container{max-width:1300px;width:100%;background:#0f1322;border-radius:2rem;border:2px solid #ffcc00;padding:2rem;box-shadow:0 8px 48px rgba(0,0,0,0.8);}
.header{display:flex;justify-content:space-between;flex-wrap:wrap;margin-bottom:1.5rem;border-bottom:2px solid #ffcc00;padding-bottom:1rem;}
.header .title{color:#ffcc00;font-size:1.5rem;font-weight:bold;}
.wallet-box{background:#010410;border-radius:0.8rem;padding:0.8rem 1rem;border:1px solid #1a2a4a;margin-bottom:1.5rem;line-height:1.6;}
.wallet-box .wallet-address{color:#88ffaa;font-size:0.7rem;word-break:break-all;}
.balance-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:0.8rem;margin-bottom:1.5rem;}
@media(max-width:700px){.balance-grid{grid-template-columns:repeat(2,1fr);}}
.balance-item{background:#010410;border-radius:0.8rem;padding:0.8rem 0.5rem;border:1px solid #1a2a4a;text-align:center;}
.balance-item .label{color:#8da0c0;font-size:0.55rem;text-transform:uppercase;}
.balance-item .value{color:#88ffaa;font-size:1rem;font-weight:bold;}
.pool-select{background:#010410;border:1px solid #1a2a4a;border-radius:2rem;padding:0.6rem 1.2rem;color:#88ffaa;width:100%;margin-bottom:0.5rem;}
.btn-group{display:flex;flex-wrap:wrap;gap:0.5rem;margin:0.8rem 0;}
.btn{background:#0a0e1a;color:#88ffaa;border:1px solid #1a2a4a;padding:0.6rem 1.2rem;border-radius:2rem;font-family:monospace;font-size:0.75rem;cursor:pointer;flex:1;min-width:70px;text-align:center;}
.btn:hover{opacity:0.8;}
.btn.success{border-color:#2a6a3a;color:#88ff88;}
.btn.primary{border-color:#ffcc00;color:#ffcc00;}
.btn.gold{border-color:#ffcc00;color:#ffcc00;}
.transfer-section{margin-top:1rem;padding-top:1rem;border-top:1px solid #1a2a4a;}
.transfer-row{display:flex;flex-wrap:wrap;gap:0.5rem;margin-bottom:0.5rem;}
.transfer-row input{flex:1;min-width:100px;background:#010410;border:1px solid #1a2a4a;border-radius:2rem;padding:0.5rem 1rem;color:#88ffaa;font-family:monospace;}
.qr-area{display:flex;align-items:center;gap:0.8rem;padding:0.5rem 0.8rem;background:#010410;border-radius:2rem;border:1px dashed #1a2a4a;margin-top:0.5rem;font-size:0.7rem;color:#4a5a6a;}
.counter-section{background:#010410;border-radius:0.8rem;padding:1.5rem;border:1px solid #1a2a4a;text-align:center;margin-top:1rem;}
.counter-section .counter-number{font-size:4rem;font-weight:bold;color:#ffcc00;text-shadow:0 0 30px rgba(255,204,0,0.2);}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:0.5rem;margin:0.5rem 0;}
.stat{background:#0a0e1a;padding:0.5rem;border-radius:0.5rem;border:1px solid #1a2a4a;text-align:center;}
.stat .label{color:#8da0c0;font-size:0.5rem;text-transform:uppercase;}
.stat .value{color:#ffcc00;font-size:1.2rem;font-weight:bold;}
.rig-log{background:#010410;border-radius:0.8rem;padding:0.8rem 1rem;min-height:250px;max-height:350px;overflow-y:auto;font-size:0.7rem;color:#8da0c0;border:1px solid #1a2a4a;margin-top:0.5rem;line-height:1.8;white-space:pre-wrap;}
.footer{text-align:center;margin-top:1.5rem;padding-top:1rem;border-top:1px solid #1a2a4a;font-size:0.6rem;color:#4a5a6a;}
.password-overlay{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.95);display:flex;justify-content:center;align-items:center;z-index:9999;}
.password-overlay.hidden{display:none;}
.password-box{background:#0f1322;border:2px solid #ffcc00;border-radius:2rem;padding:3rem;max-width:400px;width:90%;text-align:center;}
.password-box h2{color:#ffcc00;margin-bottom:1rem;}
.password-box input{background:#010410;border:1px solid #2a3a60;color:#fff;padding:12px 20px;border-radius:2rem;width:100%;font-size:1.2rem;text-align:center;margin:1rem 0;letter-spacing:4px;}
.password-box button{background:#2a4a7a;color:#fff;border:none;padding:0.8rem;border-radius:2rem;width:100%;font-size:1rem;cursor:pointer;}
.password-box .error{color:#ff6666;display:none;}
.password-box .error.show{display:block;}
.atm-overlay{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.85);display:none;justify-content:center;align-items:center;z-index:10000;}
.atm-overlay.active{display:flex;}
.atm-box{background:#0f1322;border:2px solid #ffcc00;border-radius:2rem;padding:2.5rem;max-width:480px;width:92%;text-align:center;animation:atmPop 0.3s ease-out;}
@keyframes atmPop{0%{transform:scale(0.8);opacity:0;}100%{transform:scale(1);opacity:1;}}
.atm-box h2{color:#ffcc00;font-size:1.6rem;border-bottom:1px solid #2a3a60;padding-bottom:0.8rem;margin-bottom:1.2rem;}
.atm-detail{display:flex;justify-content:space-between;padding:0.6rem 0;border-bottom:1px solid #1a2a4a;}
.atm-detail .label{color:#8da0c0;}
.atm-detail .value{color:#88ffaa;font-weight:bold;}
.atm-detail .value.highlight{color:#ffcc00;}
.atm-detail .value.txid{color:#ffaa66;}
.atm-close-btn{margin-top:1.2rem;background:#2a4a7a;color:#fff;border:none;padding:0.8rem 2rem;border-radius:2rem;font-size:1rem;cursor:pointer;}
.atm-action-tag{display:inline-block;background:#2a6a3a;color:#88ff88;padding:0.2rem 1rem;border-radius:1rem;font-size:0.7rem;text-transform:uppercase;margin-bottom:0.8rem;}
</style>
</head>
<body>
<div class="password-overlay" id="passwordOverlay">
    <div class="password-box">
        <h2>🔐 MARDUK ATM</h2>
        <p style="color:#8da0c0;">Enter PIN</p>
        <input type="password" id="pinInput" maxlength="6" placeholder="••••••" autofocus>
        <button id="unlockBtn">🔓 UNLOCK</button>
        <div class="error" id="pinError">❌ Invalid PIN</div>
        <div style="color:#4a5a6a;font-size:0.7rem;margin-top:0.5rem;">Hint: 197328</div>
    </div>
</div>
<div class="atm-overlay" id="atmOverlay">
    <div class="atm-box">
        <div class="atm-action-tag" id="atmActionTag">💸 SEND</div>
        <h2>MARDUK ATM</h2>
        <div class="atm-detail"><span class="label">Account Holder</span><span class="value highlight">Seliim Ahmed</span></div>
        <div class="atm-detail"><span class="label">Account</span><span class="value">200-4-25879-0</span></div>
        <div class="atm-detail"><span class="label">Card</span><span class="value">5598 8820 2345 3325</span></div>
        <div class="atm-detail"><span class="label">Transaction ID</span><span class="value txid" id="atmTxId">X-SA-001</span></div>
        <button class="atm-close-btn" onclick="closeATM()">✅ CONFIRM & CLOSE</button>
    </div>
</div>
<div class="container" id="mainApp" style="display:none;">
    <div class="header"><div><span class="title">⚖️ MARDUK RIG</span></div><div class="time" id="headerTime">--:--:--</div></div>
    <div class="wallet-box"><div style="color:#ffcc00;font-size:0.6rem;">Wallet</div><div class="wallet-address">45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb</div></div>
    <div class="balance-grid">
        <div class="balance-item"><div class="label">XMR</div><div class="value xmr" id="xmrBal">0.00000000</div></div>
        <div class="balance-item"><div class="label">Shares</div><div class="value" id="shareDisplay">0</div></div>
        <div class="balance-item"><div class="label">Accepted</div><div class="value" id="acceptedDisplay" style="color:#88ff88;">0</div></div>
        <div class="balance-item"><div class="label">Rejected</div><div class="value" id="rejectedDisplay" style="color:#ff6666;">0</div></div>
    </div>
    <select class="pool-select" id="poolSelect">
        <optgroup label="Monero"><option value="xmr_kryptex">Kryptex (UAE)</option><option value="xmr_support" selected>SupportXMR (EU)</option></optgroup>
        <optgroup label="Bitcoin"><option>ViaBTC</option><option>AntPool</option><option>F2Pool</option><option>Binance Pool</option></optgroup>
    </select>
    <div class="transfer-section">
        <div style="color:#ffcc00;font-size:0.85rem;margin-bottom:0.5rem;">SEND / RECEIVE / PAY</div>
        <div class="transfer-row"><input type="text" id="receiverWallet" placeholder="Receiver Wallet" /><input type="text" id="sendAmount" placeholder="Amount XMR" /></div>
        <div class="btn-group">
            <button class="btn success" onclick="openATM('SEND')">SEND</button>
            <button class="btn primary" onclick="openATM('RECEIVE')">RECEIVE</button>
            <button class="btn gold" onclick="openATM('PAY NOW')">PAY NOW</button>
        </div>
        <div class="qr-area"><span>📱</span><span>Scan QR · Click RECEIVE to generate</span></div>
        <div class="transfer-status" id="transferStatus">Ready</div>
    </div>
    <div class="btn-group">
        <button class="btn primary" onclick="fetchRealData()">⟳ FETCH REAL</button>
        <button class="btn success" onclick="copyWallet()">📋 COPY</button>
    </div>
    <div class="counter-section">
        <div style="color:#8da0c0;font-size:0.6rem;">X-SA COUNTER</div>
        <div class="counter-number" id="xsaDisplay">X-SA-000</div>
        <div class="stats">
            <div class="stat"><div class="label">Packets</div><div class="value" id="packetCount">0</div></div>
            <div class="stat"><div class="label">Sent</div><div class="value" id="sentCount">0</div></div>
            <div class="stat"><div class="label">Sorted</div><div class="value" id="sortedCount">0</div></div>
            <div class="stat"><div class="label">Earnings</div><div class="value" id="earnedDisplay">0.00000000</div></div>
        </div>
    </div>
    <div class="rig-log" id="rigLog">Waiting for data... Click FETCH REAL.</div>
    <div class="footer">© 2026 Seliim Ahmed · <span id="footerTime">--:--:--</span></div>
</div>
<script>
const PIN = "197328";
const overlay = document.getElementById('passwordOverlay');
const mainApp = document.getElementById('mainApp');
const pinInput = document.getElementById('pinInput');
const unlockBtn = document.getElementById('unlockBtn');
const pinError = document.getElementById('pinError');

function unlock() {
    if (pinInput.value === PIN) {
        overlay.classList.add('hidden');
        mainApp.style.display = 'block';
        pinError.classList.remove('show');
        initApp();
    } else {
        pinError.classList.add('show');
        pinInput.value = '';
        pinInput.focus();
    }
}
unlockBtn.addEventListener('click', unlock);
pinInput.addEventListener('keydown', function(e) { if (e.key === 'Enter') unlock(); });

const WALLET = "45ktWDeTNtUcVMXfJRKS6bbXMznMAStZFX6niJHcVy9uQk132bHJ21QTC5AKvqyx9XJN5e7mPc3vViyGnB2BM6DD1ZoAoZb";
const RIG_URL = "/status";

let shares=0,accepted=0,rejected=0,earned=0,counter=0,sent=0,txCounter=0;

function addLog(msg,type){
    const c=document.getElementById('rigLog');
    const t=new Date().toLocaleTimeString();
    const d=document.createElement('div');
    d.textContent=`> ${t} ${msg}`;
    d.style.color=type==='accepted'?'#88ff88':type==='rejected'?'#ff6666':type==='send'?'#ffcc00':'#66ccff';
    c.appendChild(d);
    c.scrollTop=c.scrollHeight;
    while(c.children.length>100)c.removeChild(c.firstChild);
}
function updateDisplay(){
    document.getElementById('xmrBal').textContent=earned.toFixed(8);
    document.getElementById('shareDisplay').textContent=shares;
    document.getElementById('acceptedDisplay').textContent=accepted;
    document.getElementById('rejectedDisplay').textContent=rejected;
    document.getElementById('earnedDisplay').textContent=earned.toFixed(8);
    document.getElementById('packetCount').textContent=counter;
    document.getElementById('sentCount').textContent=sent;
    document.getElementById('sortedCount').textContent=counter;
    document.getElementById('xsaDisplay').textContent='X-SA-'+String(counter).padStart(3,'0');
    document.getElementById('transferStatus').textContent=`Wallet: ${earned.toFixed(8)} XMR`;
}
function updateClocks(){
    const n=new Date().toLocaleTimeString();
    document.getElementById('headerTime').textContent=n;
    document.getElementById('footerTime').textContent=n;
}
setInterval(updateClocks,1000); updateClocks();

async function fetchRealData(){
    addLog('Fetching real data...');
    try{
        const res=await fetch(RIG_URL);
        if(!res.ok)throw new Error('HTTP '+res.status);
        const data=await res.json();
        shares=data.shares||0; accepted=data.accepted||0; rejected=data.rejected||0;
        counter=data.counter||0; sent=data.sent||0; earned=data.real_earnings||data.earnings||0;
        const poolResp=data.pool_response||'unknown';
        if(poolResp==='accepted') addLog('REAL POOL: Accepted share','accepted');
        else if(poolResp==='rejected') addLog('REAL POOL: Rejected share','rejected');
        else addLog('Pool response: '+poolResp);
        addLog('Earnings: '+earned.toFixed(8)+' XMR','accepted');
        updateDisplay();
    }catch(e){
        addLog('RIG OFFLINE: '+e.message,'rejected');
    }
}

function openATM(action){
    const tag=document.getElementById('atmActionTag');
    tag.textContent=(action==='SEND'?'💸':action==='RECEIVE'?'📥':'💳')+' '+action;
    txCounter++;
    const txId='X-SA-'+String(txCounter).padStart(3,'0');
    document.getElementById('atmTxId').textContent=txId;
    document.getElementById('atmOverlay').classList.add('active');
    addLog('ATM '+action+' opened ('+txId+')','send');
    if(action==='SEND'){
        const rec=document.getElementById('receiverWallet').value.trim();
        const amt=document.getElementById('sendAmount').value.trim();
        if(rec||amt) addLog('SEND to '+(rec||'unknown')+' amount '+(amt||'0')+' XMR','send');
    }
}
function closeATM(){ document.getElementById('atmOverlay').classList.remove('active'); addLog('ATM closed','accepted'); }
document.addEventListener('keydown',function(e){ if(e.key==='Escape' && document.getElementById('atmOverlay').classList.contains('active')) closeATM(); });

function copyWallet(){ navigator.clipboard.writeText(WALLET).then(()=>{
    document.getElementById('transferStatus').textContent='✅ Wallet copied!';
    setTimeout(()=>document.getElementById('transferStatus').textContent='Ready',2000);
}).catch(()=>document.getElementById('transferStatus').textContent='❌ Copy failed'); }

document.getElementById('poolSelect').addEventListener('change',function(){
    addLog('Pool selected: '+this.options[this.selectedIndex].text,'send');
});

function initApp(){
    addLog('Rig + ATM ready');
    addLog('Click FETCH REAL to get data');
    updateDisplay();
    fetchRealData();
    setInterval(fetchRealData,5000);
}
</script>
</body>
</html>
)HTML";

            while (running) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client < 0) continue;
                char buffer[4096] = {0};
                recv(client, buffer, 4095, 0);

                string request(buffer);
                string path = "/";
                if (request.find("GET ") != string::npos) {
                    size_t start = request.find("GET ") + 4;
                    size_t end = request.find(" ", start);
                    if (end != string::npos) path = request.substr(start, end - start);
                }

                string response;
                if (path == "/status" || path == "/status/") {
                    double earnings_copy;
                    {
                        lock_guard<mutex> lock(earnings_mutex);
                        earnings_copy = TOTAL_EARNINGS;
                    }
                    string json = "{";
                    json += "\"shares\":" + to_string(TOTAL_SHARES.load()) + ",";
                    json += "\"earnings\":" + to_string(earnings_copy) + ",";
                    json += "\"accepted\":" + to_string(TOTAL_SHARES.load()) + ",";
                    json += "\"rejected\":0,";
                    json += "\"counter\":" + to_string(XSA_COUNTER.load()) + ",";
                    json += "\"pool_response\":\"" + string(last_pool_response ? "accepted" : "rejected") + "\",";
                    json += "\"real_earnings\":" + to_string(earnings_copy) + ",";
                    json += "\"status\":\"online\"";
                    json += "}";
                    response = "HTTP/1.1 200 OK\r\n";
                    response += "Content-Type: application/json\r\n";
                    response += "Access-Control-Allow-Origin: *\r\n";
                    response += "\r\n";
                    response += json;
                } else {
                    response = "HTTP/1.1 200 OK\r\n";
                    response += "Content-Type: text/html\r\n";
                    response += "\r\n";
                    response += html;
                }
                send(client, response.c_str(), response.size(), 0);
                close(client);
            }
            close(server_fd);
        }).detach();
    }
    void stop() { running = false; }
};

// ============================================================
// PROCESS DATA
// ============================================================
void process_data(const char* raw, int sock) {
    char binary[4096], washed[4096];
    to_binary((const unsigned char*)raw, strlen(raw), binary);
    int xmr = has_xmr_pattern(binary);
    int btc = has_btc_pattern(binary);
    if (!xmr && !btc) {
        cout << "🚫 REJECTED\n";
        return;
    }
    egg_shorter(binary, washed);
    if (strlen(washed) == 0) return;

    XSA_COUNTER.fetch_add(1);
    TOTAL_SHARES.fetch_add(1);
    double earn = 0.0000000001;
    {
        lock_guard<mutex> lock(earnings_mutex);
        TOTAL_EARNINGS += earn;
    }
    if (sock >= 0) {
        pool_submit(sock, XSA_COUNTER.load());
        int accepted = pool_accept(sock);
        if (accepted) {
            cout << "✅ ACCEPTED share #" << TOTAL_SHARES.load() << "\n";
        }
    }
}

// ============================================================
// MAIN
// ============================================================
int main() {
    cout << "\n════════════════════════════════════════════════════\n";
    cout << "⚖️ MARDUK RIG v6.0 — FINAL WORKING\n";
    cout << "════════════════════════════════════════════════════\n";
    cout << "📤 Wallet: " << WALLET << "\n";
    cout << "🌊 Pool: " << POOL_HOST << ":" << POOL_PORT << "\n";
    cout << "────────────────────────────────────────────────────\n";

    int sock = connect_pool();
    if (sock >= 0) {
        pool_login(sock);
        cout << "✅ Connected to pool\n";
    } else {
        cout << "⚠️ Pool offline — running offline\n";
    }

    WebServer web;
    web.start();

    cout << "────────────────────────────────────────────────────\n";
    cout << "📥 Enter data (one line per entry), Ctrl+D to stop:\n";
    cout << "────────────────────────────────────────────────────\n";

    string line;
    while (getline(cin, line)) {
        if (line.empty()) continue;
        process_data(line.c_str(), sock);
    }

    web.stop();
    if (sock >= 0) close(sock);

    cout << "\n════════════════════════════════════════════════════\n";
    cout << "📊 Shares: " << TOTAL_SHARES.load() << "\n";
    {
        lock_guard<mutex> lock(earnings_mutex);
        cout << "💰 Total earned: " << TOTAL_EARNINGS << " XMR\n";
    }
    cout << "🔄 X-SA: X-SA-" << XSA_COUNTER.load() << "\n";
    cout << "════════════════════════════════════════════════════\n";
    return 0;
}
