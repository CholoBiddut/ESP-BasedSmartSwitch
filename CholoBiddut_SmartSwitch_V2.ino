#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <WebSocketsServer.h>

#define RELAY1_PIN D1
#define RELAY2_PIN D2
#define BTN1_PIN    D5
#define BTN2_PIN    D6
#define AP_BTN_PIN  D7
#define LED_PIN     D0

const char* mdnsName = "cholobiddut";
const char* apName = "CholoBiddut Smart Home";

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
WiFiManager wm;

bool relay1State = false;
bool relay2State = false;

bool btn1Last = HIGH;
bool btn2Last = HIGH;
bool apBtnLast = HIGH;

unsigned long lastBtn1Change = 0;
unsigned long lastBtn2Change = 0;
unsigned long lastApChange = 0;

unsigned long apHoldStart = 0;
bool apHoldActive = false;
bool apTriggered = false;

const uint8_t DEBOUNCE_MS = 50;
const uint32_t AP_HOLD_MS = 5000;

bool setupPortalActive = false;
bool wifiReady = false;

unsigned long ledLastToggle = 0;
bool ledState = LOW;

const int RELAY_ON = LOW;
const int RELAY_OFF = HIGH;

String escapeJson(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == '"') o += '\\';
    o += c;
  }
  return o;
}

void applyRelay(uint8_t pin, bool state) {
  digitalWrite(pin, state ? RELAY_ON : RELAY_OFF);
}

void sendStateToClients() {
  String payload = "{";
  payload += "\"r1\":" + String(relay1State ? "true" : "false") + ",";
  payload += "\"r2\":" + String(relay2State ? "true" : "false") + ",";
  payload += "\"wifi\":\"" + escapeJson(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : (setupPortalActive ? String(apName) : String("Connecting"))) + "\",";
  payload += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  payload += "}";
  webSocket.broadcastTXT(payload);
}

void syncOutputs() {
  applyRelay(RELAY1_PIN, relay1State);
  applyRelay(RELAY2_PIN, relay2State);
  sendStateToClients();
}

void setRelay1(bool state) { relay1State = state; syncOutputs(); }
void setRelay2(bool state) { relay2State = state; syncOutputs(); }
void toggleRelay1() { setRelay1(!relay1State); }
void toggleRelay2() { setRelay2(!relay2State); }

String pageHtml() {
  String s;
  s.reserve(4200);
  s += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>";
  s += "<title>Dashboard</title>";
  s += "<style>";
  s += "body{margin:0;font-family:Arial,sans-serif;background:#f4f7fb;color:#111}";
  s += ".wrap{max-width:520px;margin:0 auto;padding:14px 14px 76px}";
  s += ".card{background:#fff;border-radius:18px;padding:14px;box-shadow:0 8px 22px rgba(0,0,0,.08);margin-bottom:12px}";
  s += ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}";
  s += ".btn{width:100%;padding:18px 12px;border:0;border-radius:16px;font-size:18px;font-weight:700}";
  s += ".on{background:#16a34a;color:#fff}.off{background:#dc2626;color:#fff}.gray{background:#334155;color:#fff}";
  s += ".top{display:flex;justify-content:space-between;align-items:center;gap:10px}";
  s += ".pill{display:inline-block;padding:8px 10px;border-radius:999px;background:#e2e8f0;font-size:13px}";
  s += ".iconbtn{width:44px;height:44px;border-radius:14px;border:0;background:#e2e8f0;font-size:20px}";
  s += ".hidden{display:none}.footer{text-align:center;color:#64748b;font-size:13px;position:fixed;left:0;right:0;bottom:0;padding:14px;background:linear-gradient(to top,#f4f7fb 70%,rgba(244,247,251,0))}";
  s += ".small{font-size:13px;color:#475569;line-height:1.45}";
  s += ".linkbox{margin-top:10px;padding:12px;border-radius:14px;background:#f8fafc;border:1px solid #e2e8f0}";
  s += "</style></head><body><div class='wrap'>";
  s += "<div class='card'><div class='top'><div><div style='font-size:20px;font-weight:800'>Home Control</div><div class='small'>WiFi: <span id='wifi'>...</span> · IP: <span id='ip'>...</span></div></div><button class='iconbtn' onclick='toggleSetup()'>⚙️</button></div>";
  s += "<div id='setupInfo' class='linkbox hidden'><div class='small'>Press and hold the configure button for 5 seconds to enter AP mode.</div></div></div>";
  s += "<div class='card'><div class='row'><button id='b1' class='btn off' onclick='toggle(1)'>Switch 1 OFF</button><button id='b2' class='btn off' onclick='toggle(2)'>Switch 2 OFF</button></div></div>";
  s += "<div class='card'><button class='btn gray' onclick='toggleSetup()'>WiFi Setup</button></div>";
  s += "</div><div class='footer'>Powered by CholoBiddut © 2026 CholoBiddut. All rights reserved.</div>";
  s += "<script>";
  s += "let ws;function conn(){ws=new WebSocket('ws://'+location.hostname+':81/');ws.onopen=()=>ws.send('get');";
  s += "ws.onmessage=e=>{let d=JSON.parse(e.data);document.getElementById('wifi').textContent=d.wifi;document.getElementById('ip').textContent=d.ip;upd(1,d.r1);upd(2,d.r2)};";
  s += "ws.onclose=()=>setTimeout(conn,600)}conn();";
  s += "function upd(n,v){let b=document.getElementById('b'+n);if(v){b.className='btn on';b.textContent='Switch '+n+' ON'}else{b.className='btn off';b.textContent='Switch '+n+' OFF'}}";
  s += "function toggle(n){if(ws&&ws.readyState===1)ws.send('toggle:'+n)}";
  s += "function toggleSetup(){document.getElementById('setupInfo').classList.toggle('hidden');if(ws&&ws.readyState===1)ws.send('get')}";
  s += "</script></body></html>";
  return s;
}

void handleRoot() { server.send(200, "text/html", pageHtml()); }

void handleControl() {
  if (server.hasArg("id") && server.hasArg("state")) {
    int id = server.arg("id").toInt();
    bool st = server.arg("state") == "1";
    if (id == 1) relay1State = st;
    if (id == 2) relay2State = st;
    syncOutputs();
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }
  server.send(400, "application/json", "{\"ok\":false}");
}

void startAPMode() {
  setupPortalActive = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName);
  MDNS.end();
  MDNS.begin(mdnsName);
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);
  wm.setConfigPortalTimeout(180);
  wm.startConfigPortal(apName);
  setupPortalActive = false;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type != WStype_TEXT) return;
  String msg = String((char*)payload);
  if (msg == "get") sendStateToClients();
  else if (msg == "toggle:1") toggleRelay1();
  else if (msg == "toggle:2") toggleRelay2();
  else if (msg == "ap") startAPMode();
}

void blinkLed(uint32_t intervalMs) {
  unsigned long now = millis();
  if (now - ledLastToggle >= intervalMs) {
    ledLastToggle = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
}

void updateStatusLed() {
  if (WiFi.status() != WL_CONNECTED || setupPortalActive) {
    blinkLed(300);
  } else {
    blinkLed(2000);
  }
}

void handleButtons() {
  unsigned long now = millis();

  bool b1 = digitalRead(BTN1_PIN);
  bool b2 = digitalRead(BTN2_PIN);
  bool apb = digitalRead(AP_BTN_PIN);

  if (b1 != btn1Last && (now - lastBtn1Change) > DEBOUNCE_MS) {
    lastBtn1Change = now;
    btn1Last = b1;
    if (b1 == LOW) toggleRelay1();
  }

  if (b2 != btn2Last && (now - lastBtn2Change) > DEBOUNCE_MS) {
    lastBtn2Change = now;
    btn2Last = b2;
    if (b2 == LOW) toggleRelay2();
  }

  if (apb != apBtnLast && (now - lastApChange) > DEBOUNCE_MS) {
    lastApChange = now;
    apBtnLast = apb;
    if (apb == LOW) {
      apHoldStart = now;
      apHoldActive = true;
      apTriggered = false;
    } else {
      apHoldActive = false;
    }
  }

  if (apHoldActive && !apTriggered && apb == LOW && (now - apHoldStart) >= AP_HOLD_MS) {
    apTriggered = true;
    startAPMode();
  }
}

void setupWiFi() {
  wm.setConfigPortalTimeout(180);
  bool ok = wm.autoConnect(apName);
  wifiReady = ok;
  if (!ok) {
    setupPortalActive = true;
  }
}

void setup() {
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(AP_BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  applyRelay(RELAY1_PIN, false);
  applyRelay(RELAY2_PIN, false);

  LittleFS.begin();
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  setupWiFi();

  WiFi.hostname(mdnsName);
  if (MDNS.begin(mdnsName)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 81);
  }

  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  sendStateToClients();
}

void loop() {
  server.handleClient();
  webSocket.loop();
  MDNS.update();
  handleButtons();
  updateStatusLed();
}