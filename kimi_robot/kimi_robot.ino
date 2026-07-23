/*
 * Kimi WiFi + BLE Controller (Multi-WiFi edition)
 * Hardware: ESP32-S3
 *   - WS2812 RGB LED on GPIO48
 *   - Servo on GPIO13
 *   - BLE UART GATT server, name 'Kimi-Robot'
 *   - HTTP server on port 80
 *
 * WiFi: 优先连接列表中的第一个可用网络，失败则尝试下一个。
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>

// ========== WiFi 配置（按优先级从上到下）==========
struct WifiCred { const char* ssid; const char* pass; };
static const WifiCred wifiList[] = {
  { "DESKTOP-EL1SUEF 1389", "16897pW]" },   // 优先：电脑热点
  { "vivo S60",             "youjiahao"  },  // 备用：手机热点
};
static const int wifiCount = sizeof(wifiList) / sizeof(wifiList[0]);

// ========== 引脚定义 ==========
#define PIN_LED     48   // WS2812
#define PIN_SERVO   13
#define NUM_PIXELS   1

// ========== BLE UART Service ==========
static BLEUUID  SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static BLEUUID  CHAR_RX_UUID ("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); // 写
static BLEUUID  CHAR_TX_UUID ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // 通知

// ========== 全局对象 ==========
WebServer          server(80);
Adafruit_NeoPixel  pixel(NUM_PIXELS, PIN_LED, NEO_GRB + NEO_KHZ800);
Servo              servo;
BLEServer*         bleServer = nullptr;
BLECharacteristic* txChar    = nullptr;
bool               bleConnected = false;

// 状态
static int  servoAngle = 90;
static uint8_t ledR = 0, ledG = 0, ledB = 0;

// ========== 工具函数 ==========
static void setLED(uint8_t r, uint8_t g, uint8_t b) {
  ledR = r; ledG = g; ledB = b;
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
  Serial.printf("[LED] R=%d G=%d B=%d\n", r, g, b);
}

static void setServo(int deg) {
  deg = constrain(deg, 0, 180);
  servoAngle = deg;
  servo.write(deg);
  Serial.printf("[Servo] -> %d deg\n", deg);
}

// ========== BLE 回调 ==========
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConnected = true;  Serial.println("[BLE] Client connected"); }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    Serial.println("[BLE] Client disconnected");
    s->getAdvertising()->start();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String s = c->getValue();
    if (s.length() == 0) return;
    s.trim();
    Serial.printf("[BLE RX] %s\n", s.c_str());
    handleCommand(s);
  }
};

// ========== 命令处理（BLE 和 HTTP 共用）==========
// 命令格式（不区分大小写）：
//   LED,R,G,B        例如 LED,255,0,0
//   SERVO,DEG        例如 SERVO,90
//   STATUS           返回状态 JSON
//   PING             返回 PONG
String handleCommand(const String& cmd) {
  String upper = cmd;
  upper.toUpperCase();
  upper.trim();
  char valBuf[16];

  if (upper == "PING") {
    return "PONG";
  }
  if (upper == "MEM") {
    String j;
    j.reserve(256);
    j += "{\"heap\":{\"free\":" + String(ESP.getFreeHeap()) +
         ",\"min\":" + String(ESP.getMinFreeHeap()) +
         ",\"max_alloc\":" + String(ESP.getMaxAllocHeap()) + "},";
    j += "\"sketch\":{\"used\":" + String(ESP.getSketchSize()) +
         ",\"free\":" + String(ESP.getFreeSketchSpace()) + "},";
    j += "\"chip\":{\"model\":\"" + String(ESP.getChipModel()) +
         "\",\"rev\":" + String(ESP.getChipRevision()) +
         ",\"cores\":" + String(ESP.getChipCores()) +
         ",\"cpu_mhz\":" + String(ESP.getCpuFreqMHz()) + "},";
    j += "\"flash_size\":" + String(ESP.getFlashChipSize()) +
         ",\"psram_size\":" + String(ESP.getPsramSize()) +
         ",\"free_psram\":" + String(ESP.getFreePsram()) + "}";
    return j;
  }
  if (upper == "STATUS") {
    String j;
    j.reserve(220);
    j += "{\"wifi\":{\"ssid\":\"" + String(WiFi.SSID()) + "\",\"ip\":\"" + WiFi.localIP().toString() + "\"},";
    j += "\"ble\":" + String(bleConnected ? "true" : "false") + ",";
    j += "\"led\":{\"r\":" + String(ledR) + ",\"g\":" + String(ledG) + ",\"b\":" + String(ledB) + "},";
    j += "\"servo\":" + String(servoAngle) + ",";
    j += "\"heap_free\":" + String(ESP.getFreeHeap()) + ",";
    j += "\"heap_min\":" + String(ESP.getMinFreeHeap()) + "}";
    return j;
  }
  if (upper.startsWith("LED,")) {
    int r, g, b;
    if (sscanf(cmd.c_str(), "LED,%d,%d,%d", &r, &g, &b) == 3) {
      setLED(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
      return "OK LED";
    }
    return "ERR LED format: LED,R,G,B";
  }
  if (upper.startsWith("SERVO,")) {
    int deg;
    if (sscanf(cmd.c_str(), "SERVO,%d", &deg) == 1) {
      setServo(deg);
      return "OK SERVO";
    }
    return "ERR SERVO format: SERVO,DEG";
  }
  if (upper.startsWith("PIN,")) {
    int pin = 0; String val = "";
    if (sscanf(cmd.c_str(), "PIN,%d,%s", &pin, valBuf) >= 1) {
      val = String(valBuf); val.trim(); val.toUpperCase();
      servo.detach();
      pinMode(pin, OUTPUT);
      if (val == "HIGH" || val == "1" || val == "ON") {
        digitalWrite(pin, HIGH);
      } else {
        digitalWrite(pin, LOW);
      }
      return "OK PIN" + String(pin) + "=" + val;
    }
    return "ERR PIN format: PIN,pin,HIGH|LOW";
  }
  return "ERR unknown cmd: " + cmd;
}

// ========== HTTP 路由 ==========
static void httpRoot() {
  String ip = WiFi.localIP().toString();
  String html = String(
    "<!doctype html><html lang='zh'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
    "<meta name='theme-color' content='#0b1120'>"
    "<title>Kimi Robot</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}"
    "html,body{height:100%}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','PingFang SC',sans-serif;"
    "background:radial-gradient(ellipse at top,#1e293b,#0b1120 60%);color:#e2e8f0;"
    "min-height:100vh;padding:16px;max-width:520px;margin:0 auto;overflow-x:hidden}"
    ".hdr{text-align:center;padding:18px 0 10px;position:relative}"
    ".hdr h1{font-size:26px;font-weight:700;background:linear-gradient(135deg,#22d3ee,#a78bfa,#f472b6);"
    "-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;letter-spacing:.5px}"
    ".hdr .sub{font-size:12px;color:#64748b;margin-top:6px;display:flex;align-items:center;justify-content:center;gap:8px}"
    ".dot{width:6px;height:6px;border-radius:50%;background:#22c55e;box-shadow:0 0 8px #22c55e;animation:pulse 2s infinite}"
    "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}"
    ".hdr .ip{font-size:11px;color:#475569;margin-top:4px;font-family:ui-monospace,monospace}"
    ".card{background:rgba(30,41,59,.55);border:1px solid rgba(148,163,184,.08);"
    "border-radius:20px;padding:18px;margin-bottom:14px;backdrop-filter:blur(16px);"
    "box-shadow:0 8px 32px rgba(0,0,0,.25),inset 0 1px 0 rgba(255,255,255,.04)}"
    ".card h3{font-size:12px;font-weight:600;color:#94a3b8;margin-bottom:14px;"
    "text-transform:uppercase;letter-spacing:1.5px;display:flex;align-items:center;gap:8px}"
    ".card h3::before{content:'';width:4px;height:14px;background:linear-gradient(180deg,#22d3ee,#a78bfa);border-radius:2px}"
    ".led-preview{width:100%;height:80px;border-radius:14px;margin-bottom:16px;"
    "background:#0f172a;position:relative;overflow:hidden;border:1px solid rgba(148,163,184,.1)}"
    ".led-preview .glow{position:absolute;inset:0;background:radial-gradient(ellipse at center,var(--c,rgba(34,211,238,.4)),transparent 70%);"
    "transition:background .2s}"
    ".led-preview .val{position:absolute;bottom:10px;right:12px;font-family:ui-monospace,monospace;"
    "font-size:11px;color:#94a3b8;letter-spacing:.5px}"
    ".rgb-wrap{display:flex;flex-direction:column;gap:12px}"
    ".rgb-row{display:flex;align-items:center;gap:10px}"
    ".rgb-lab{width:22px;font-size:11px;font-weight:700;text-align:center;border-radius:6px;"
    "padding:4px 0;color:#fff;font-family:ui-monospace,monospace}"
    ".rgb-lab.r{background:linear-gradient(135deg,#ef4444,#dc2626)}"
    ".rgb-lab.g{background:linear-gradient(135deg,#22c55e,#16a34a)}"
    ".rgb-lab.b{background:linear-gradient(135deg,#3b82f6,#2563eb)}"
    ".rgb-num{width:44px;text-align:right;font-family:ui-monospace,monospace;font-size:13px;color:#cbd5e1;font-weight:600}"
    "input[type=range].rgb{-webkit-appearance:none;flex:1;height:28px;border-radius:14px;"
    "outline:none;background:transparent;position:relative}"
    "input[type=range].rgb::-webkit-slider-runnable-track{height:6px;border-radius:3px;"
    "background:linear-gradient(90deg,#1e293b 0%,var(--tc) 100%)}"
    "input[type=range].rgb::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;"
    "border-radius:50%;background:#fff;cursor:pointer;margin-top:-8px;"
    "box-shadow:0 2px 10px rgba(0,0,0,.4),0 0 0 3px rgba(255,255,255,.1)}"
    ".quick-colors{display:grid;grid-template-columns:repeat(6,1fr);gap:8px;margin-top:14px}"
    ".qc{height:32px;border-radius:10px;border:none;cursor:pointer;transition:transform .12s,box-shadow .2s;"
    "background:var(--c);box-shadow:0 2px 8px rgba(0,0,0,.3)}"
    ".qc:active{transform:scale(.9)}"
    ".qc.off{background:#334155;border:1px solid #475569}"
    ".joystick-wrap{display:flex;flex-direction:column;align-items:center;gap:12px}"
    ".joy-base{width:220px;height:220px;border-radius:50%;"
    "background:radial-gradient(circle,#1e293b 0%,#0f172a 70%);"
    "border:2px solid rgba(148,163,184,.15);position:relative;touch-action:none;cursor:pointer;"
    "box-shadow:inset 0 4px 20px rgba(0,0,0,.5),0 4px 20px rgba(0,0,0,.3)}"
    ".joy-base::before{content:'';position:absolute;inset:20px;border-radius:50%;"
    "border:1px dashed rgba(148,163,184,.15)}"
    ".joy-base::after{content:'';position:absolute;inset:50px;border-radius:50%;"
    "border:1px dashed rgba(148,163,184,.1)}"
    ".joy-stick{position:absolute;left:50%;top:50%;width:70px;height:70px;border-radius:50%;"
    "background:radial-gradient(circle at 30% 30%,#64748b,#1e293b);"
    "border:3px solid rgba(148,163,184,.3);transform:translate(-50%,-50%);"
    "box-shadow:0 6px 20px rgba(0,0,0,.5),inset 0 2px 6px rgba(255,255,255,.1);"
    "pointer-events:none;transition:width .1s,height .1s}"
    ".joy-stick.active{width:66px;height:66px}"
    ".joy-info{display:flex;align-items:center;gap:14px;font-family:ui-monospace,monospace}"
    ".joy-info .angle{font-size:28px;font-weight:700;background:linear-gradient(135deg,#22d3ee,#a78bfa);"
    "-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;min-width:80px;text-align:center}"
    ".joy-info .lbl{font-size:11px;color:#64748b;text-transform:uppercase;letter-spacing:1px}"
    ".joy-center-btn{background:#334155;border:none;border-radius:10px;padding:8px 14px;color:#cbd5e1;"
    "font-size:12px;font-weight:500;cursor:pointer;transition:background .15s}"
    ".joy-center-btn:active{background:#475569}"
    ".status-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;font-size:12px}"
    ".status-item{background:#0f172a;border-radius:10px;padding:10px 12px;border:1px solid rgba(148,163,184,.06)}"
    ".status-item .k{font-size:10px;color:#64748b;text-transform:uppercase;letter-spacing:1px;margin-bottom:4px}"
    ".status-item .v{font-family:ui-monospace,monospace;font-size:13px;color:#e2e8f0;font-weight:500;word-break:break-all}"
    ".status-item .v.good{color:#22c55e}"
    ".status-item .v.warn{color:#f59e0b}"
    "</style></head><body>"
    "<div class='hdr'>"
    "<h1>Kimi Robot</h1>"
    "<div class='sub'><span class='dot'></span><span>在线</span></div>"
    "<div class='ip'>") + ip + String("</div>"
    "</div>"

    "<div class='card'>"
    "<h3>LED 调色</h3>"
    "<div class='led-preview' id='lp'><div class='glow' id='lg'></div><div class='val' id='lv'>RGB(0,0,0)</div></div>"
    "<div class='rgb-wrap'>"
    "<div class='rgb-row'><span class='rgb-lab r'>R</span>"
    "<input type='range' class='rgb' id='r' min='0' max='255' value='0' style='--tc:#ef4444' oninput='updLED()'>"
    "<span class='rgb-num' id='rv'>0</span></div>"
    "<div class='rgb-row'><span class='rgb-lab g'>G</span>"
    "<input type='range' class='rgb' id='g' min='0' max='255' value='0' style='--tc:#22c55e' oninput='updLED()'>"
    "<span class='rgb-num' id='gv'>0</span></div>"
    "<div class='rgb-row'><span class='rgb-lab b'>B</span>"
    "<input type='range' class='rgb' id='b' min='0' max='255' value='0' style='--tc:#3b82f6' oninput='updLED()'>"
    "<span class='rgb-num' id='bv'>0</span></div>"
    "</div>"
    "<div class='quick-colors'>"
    "<button class='qc' style='--c:#ef4444' onclick=\"setLED(255,0,0)\"></button>"
    "<button class='qc' style='--c:#f97316' onclick=\"setLED(249,115,22)\"></button>"
    "<button class='qc' style='--c:#eab308' onclick=\"setLED(234,179,8)\"></button>"
    "<button class='qc' style='--c:#22c55e' onclick=\"setLED(34,197,94)\"></button>"
    "<button class='qc' style='--c:#3b82f6' onclick=\"setLED(59,130,246)\"></button>"
    "<button class='qc' style='--c:#a855f7' onclick=\"setLED(168,85,247)\"></button>"
    "<button class='qc' style='--c:#ec4899' onclick=\"setLED(236,72,153)\"></button>"
    "<button class='qc' style='--c:#14b8a6' onclick=\"setLED(20,184,166)\"></button>"
    "<button class='qc' style='--c:#f8fafc' onclick=\"setLED(255,255,255)\"></button>"
    "<button class='qc off' onclick=\"setLED(0,0,0)\">关</button>"
    "</div>"
    "</div>"

    "<div class='card'>"
    "<h3>舵机摇杆</h3>"
    "<div class='joystick-wrap'>"
    "<div class='joy-base' id='jb'><div class='joy-stick' id='js'></div></div>"
    "<div class='joy-info'>"
    "<div><div class='lbl'>角度</div><div class='angle' id='sa'>90°</div></div>"
    "<button class='joy-center-btn' onclick='centerServo()'>回 中</button>"
    "</div>"
    "</div>"
    "</div>"

    "<div class='card'>"
    "<h3>设备状态</h3>"
    "<div class='status-grid' id='sg'>"
    "<div class='status-item'><div class='k'>WiFi</div><div class='v' id='sw'>--</div></div>"
    "<div class='status-item'><div class='k'>IP</div><div class='v' id='si'>--</div></div>"
    "<div class='status-item'><div class='k'>BLE</div><div class='v' id='sb'>--</div></div>"
    "<div class='status-item'><div class='k'>舵机</div><div class='v' id='ss'>--</div></div>"
    "</div>"
    "</div>"

    "<script>"
    "var ledTimer=null,svTimer=null;"
    "function $(id){return document.getElementById(id)}"
    "function sendCmd(cmd){"
    "fetch('/cmd?c='+encodeURIComponent(cmd))"
    ".then(function(r){return r.text()})"
    ".then(function(t){try{var d=JSON.parse(t);updStatus(d)}catch(e){}})"
    ".catch(function(e){})"
    "}"
    "function updLED(){"
    "var r=+$('r').value,g=+$('g').value,b=+$('b').value;"
    "$('rv').textContent=r;$('gv').textContent=g;$('bv').textContent=b;"
    "var c='rgb('+r+','+g+','+b+')';"
    "$('lg').style.background='radial-gradient(ellipse at center,'+c+')';$('lv').textContent='RGB('+r+','+g+','+b+')';"
    "clearTimeout(ledTimer);ledTimer=setTimeout(function(){sendCmd('LED,'+r+','+g+','+b)},80)"
    "}"
    "function setLED(r,g,b){$('r').value=r;$('g').value=g;$('b').value=b;updLED()}"
    "function updStatus(d){"
    "$('sw').textContent=d.wifi.ssid||'--';$('sw').className='v '+(d.wifi.ssid?'good':'warn');"
    "$('si').textContent=d.wifi.ip||'--';"
    "$('sb').textContent=d.ble?'已连接':'待机';$('sb').className='v '+(d.ble?'good':'warn');"
    "$('ss').textContent=d.servo+'°';"
    "}"
    "function loadStatus(){fetch('/cmd?c=STATUS').then(function(r){return r.text()}).then(function(t){try{updStatus(JSON.parse(t))}catch(e){}})}"
    "loadStatus();setInterval(loadStatus,4000);"

    "var jb=$('jb'),js=$('js'),jd=false,cr=75,sa=90;"
    "function setServo(a){a=Math.max(0,Math.min(180,a));sa=a;$('sa').textContent=a+'°';"
    "clearTimeout(svTimer);svTimer=setTimeout(function(){sendCmd('SERVO,'+Math.round(a))},60)}"
    "function centerServo(){setServo(90);js.style.left='50%';js.style.top='50%'}"
    "function jPos(e){"
    "var r=jb.getBoundingClientRect();"
    "var t=e.touches?e.touches[0]:e;"
    "var x=t.clientX-r.left-r.width/2,y=t.clientY-r.top-r.height/2;"
    "var d=Math.sqrt(x*x+y*y);if(d>cr){x=x*cr/d;y=y*cr/d}"
    "js.style.left=(50+x/r.width*100)+'%';js.style.top=(50+y/r.height*100)+'%';"
    "var ang=90-Math.atan2(-y,x)*180/Math.PI;"
    "if(ang<0)ang+=360;var norm=d/cr;var pos=90-(x/cr)*90;setServo(Math.round(pos))"
    "}"
    "jb.addEventListener('mousedown',function(e){jd=true;js.classList.add('active');jPos(e);e.preventDefault()});"
    "document.addEventListener('mousemove',function(e){if(jd){jPos(e)}});"
    "document.addEventListener('mouseup',function(){if(jd){jd=false;js.classList.remove('active')}});"
    "jb.addEventListener('touchstart',function(e){jd=true;js.classList.add('active');jPos(e);e.preventDefault()},{passive:false});"
    "jb.addEventListener('touchmove',function(e){if(jd){jPos(e)}e.preventDefault()},{passive:false});"
    "jb.addEventListener('touchend',function(){jd=false;js.classList.remove('active')});"
    "</script></body></html>");
  server.send(200, "text/html", html);
}

static void httpCmd() {
  if (!server.hasArg("c")) { server.send(400, "text/plain", "missing c"); return; }
  String resp = handleCommand(server.arg("c"));
  server.send(200, "text/plain", resp);
}

static void httpStatus() {
  server.send(200, "application/json", handleCommand("STATUS"));
}

// ========== BLE 初始化 ==========
static void setupBLE() {
  BLEDevice::init("Kimi-Robot");
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  BLEService* svc = bleServer->createService(SERVICE_UUID);

  BLECharacteristic* rx = svc->createCharacteristic(
      CHAR_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new RxCallbacks());

  txChar = svc->createCharacteristic(
      CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  svc->start();
  BLEAdvertising* adv = bleServer->getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
  Serial.println("[BLE] Advertising as 'Kimi-Robot' (UART mode)");
}

// ========== WiFi 初始化（扫描模式）==========
// 扫描周围 WiFi，按 wifiList 顺序匹配第一个扫到的 SSID 进行连接。
static void setupWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.println("[WiFi] Scanning networks...");
  int n = WiFi.scanNetworks();
  Serial.printf("[WiFi] Found %d networks\n", n);

  const WifiCred* chosen = nullptr;
  if (n > 0) {
    // 列出扫描结果（调试用）
    for (int i = 0; i < n; i++) {
      Serial.printf("  - %s (%d dBm) %s\n",
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "[open]" : "[secure]");
    }
    // 按 wifiList 优先级匹配
    for (int k = 0; k < wifiCount && !chosen; k++) {
      for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == wifiList[k].ssid) {
          chosen = &wifiList[k];
          Serial.printf("[WiFi] Matched '%s' (%d dBm)\n",
                        chosen->ssid, WiFi.RSSI(i));
          break;
        }
      }
    }
  }

  if (!chosen) {
    Serial.println("[WiFi] No known SSID found in scan, trying list in order...");
    // 扫描没匹配到也尝试一下（防止隐藏 SSID）
    for (int k = 0; k < wifiCount && !chosen; k++) {
      chosen = &wifiList[k];
    }
  }

  if (chosen) {
    Serial.printf("[WiFi] Connecting to '%s'...\n", chosen->ssid);
    WiFi.begin(chosen->ssid, chosen->pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(400);
      Serial.print(".");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s (SSID: %s)\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.SSID().c_str());
  } else {
    Serial.println("\n[WiFi] All attempts failed, continuing without WiFi.");
  }
  WiFi.scanDelete();
}

// ========== Arduino 入口 ==========
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=============================");
  Serial.println("  Kimi WiFi + BLE Controller");
  Serial.println("  (Multi-WiFi edition)");
  Serial.println("=============================");

  // LED
  pixel.begin();
  pixel.clear();
  pixel.show();
  setLED(0, 0, 0);
  Serial.println("[LED] GPIO48 WS2812 ready");

  // Servo
  ESP32PWM::allocateTimer(0);
  servo.setPeriodHertz(50);
  servo.attach(PIN_SERVO, 1000, 2000);
  setServo(90);
  Serial.println("[Servo] GPIO13 ready, 90 deg");

  // WiFi
  setupWiFi();

  // HTTP
  server.on("/",        HTTP_GET, httpRoot);
  server.on("/cmd",     HTTP_GET, httpCmd);
  server.on("/status",  HTTP_GET, httpStatus);
  server.begin();
  Serial.println("[HTTP] Server started (port 80)");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("  +-------------------------------------+");
    Serial.println("  |  WiFi: http://" + WiFi.localIP().toString());
    Serial.println("  |  BLE:  nRF Connect -> 'Kimi-Robot'");
    Serial.println("  +-------------------------------------+");
  }

  // BLE
  setupBLE();

  // 启动指示灯
  setLED(0, 150, 136);
}

void loop() {
  server.handleClient();
  delay(2);
}
