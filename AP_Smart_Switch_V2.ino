#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <vector>

#define RELAY1_PIN D1
#define RELAY2_PIN D2
#define BTN1_PIN    D5
#define BTN2_PIN    D6
#define CFG_BTN_PIN D7
#define LED_PIN     D0

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);

struct Settings {
  String apSsid = "CholoBiddut Smart Home";
  String apPass = "12345678";
  String mdnsName = "cholobiddut";
  String sw1Name = "Switch 1";
  String sw2Name = "Switch 2";
  uint32_t defaultTimerSec = 300;
} cfg;

bool relay1State = false;
bool relay2State = false;

bool relay1TimerActive = false;
bool relay2TimerActive = false;
uint32_t relay1TimerEnd = 0;
uint32_t relay2TimerEnd = 0;
uint32_t relay1TimerTotal = 0;
uint32_t relay2TimerTotal = 0;

bool btn1Last = HIGH, btn2Last = HIGH, cfgBtnLast = HIGH;
unsigned long lastBtn1Change = 0, lastBtn2Change = 0, lastCfgChange = 0;
unsigned long cfgHoldStart = 0;
bool cfgHoldActive = false;
bool cfgTriggered = false;

bool portalActive = false;

unsigned long ledLastToggle = 0;
bool ledState = LOW;

const uint8_t DEBOUNCE_MS = 50;
const uint32_t CFG_HOLD_MS = 5000;
const int RELAY_ON = LOW;
const int RELAY_OFF = HIGH;

String esc(const String& s) {
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

void saveSettings() {
  File f = LittleFS.open("/config.txt", "w");
  if (!f) return;
  f.println(cfg.apSsid);
  f.println(cfg.apPass);
  f.println(cfg.mdnsName);
  f.println(cfg.sw1Name);
  f.println(cfg.sw2Name);
  f.println(String(cfg.defaultTimerSec));
  f.close();
}

void loadSettings() {
  if (!LittleFS.exists("/config.txt")) return;
  File f = LittleFS.open("/config.txt", "r");
  if (!f) return;
  cfg.apSsid = f.readStringUntil('\n'); cfg.apSsid.trim();
  cfg.apPass = f.readStringUntil('\n'); cfg.apPass.trim();
  cfg.mdnsName = f.readStringUntil('\n'); cfg.mdnsName.trim();
  cfg.sw1Name = f.readStringUntil('\n'); cfg.sw1Name.trim();
  cfg.sw2Name = f.readStringUntil('\n'); cfg.sw2Name.trim();
  String t = f.readStringUntil('\n'); t.trim();
  if (t.length()) cfg.defaultTimerSec = t.toInt();
  f.close();

  if (cfg.apSsid.length() == 0) cfg.apSsid = "CholoBiddut Smart Home";
  if (cfg.apPass.length() < 8) cfg.apPass = "12345678";
  if (cfg.mdnsName.length() == 0) cfg.mdnsName = "cholobiddut";
  if (cfg.sw1Name.length() == 0) cfg.sw1Name = "Switch 1";
  if (cfg.sw2Name.length() == 0) cfg.sw2Name = "Switch 2";
  if (cfg.defaultTimerSec == 0) cfg.defaultTimerSec = 300;
}

uint32_t remainingSec(uint32_t endMs) {
  unsigned long now = millis();
  if ((long)(endMs - now) <= 0) return 0;
  return (endMs - now + 999) / 1000;
}

void sendState() {
  String p = "{";
  p += "\"r1\":" + String(relay1State ? "true" : "false") + ",";
  p += "\"r2\":" + String(relay2State ? "true" : "false") + ",";
  p += "\"n1\":\"" + esc(cfg.sw1Name) + "\",";
  p += "\"n2\":\"" + esc(cfg.sw2Name) + "\",";
  p += "\"ssid\":\"" + esc(cfg.apSsid) + "\",";
  p += "\"mdns\":\"" + esc(cfg.mdnsName) + "\",";
  p += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  p += "\"t1\":" + String(relay1TimerActive ? remainingSec(relay1TimerEnd) : 0) + ",";
  p += "\"t2\":" + String(relay2TimerActive ? remainingSec(relay2TimerEnd) : 0) + ",";
  p += "\"c1\":" + String(relay1TimerActive ? remainingSec(relay1TimerEnd) : 0) + ",";
  p += "\"c2\":" + String(relay2TimerActive ? remainingSec(relay2TimerEnd) : 0) + ",";
  p += "\"d\":\"" + String(cfg.defaultTimerSec) + "\"";
  p += "}";
  webSocket.broadcastTXT(p);
}

void syncOutputs() {
  applyRelay(RELAY1_PIN, relay1State);
  applyRelay(RELAY2_PIN, relay2State);
  sendState();
}

void setRelay1(bool state) {
  relay1State = state;
  if (!state) relay1TimerActive = false;
  syncOutputs();
}
void setRelay2(bool state) {
  relay2State = state;
  if (!state) relay2TimerActive = false;
  syncOutputs();
}

void toggleRelay1() { setRelay1(!relay1State); }
void toggleRelay2() { setRelay2(!relay2State); }

void startTimer1(uint32_t sec) {
  relay1State = true;
  relay1TimerActive = true;
  relay1TimerTotal = sec;
  relay1TimerEnd = millis() + sec * 1000UL;
  syncOutputs();
}
void startTimer2(uint32_t sec) {
  relay2State = true;
  relay2TimerActive = true;
  relay2TimerTotal = sec;
  relay2TimerEnd = millis() + sec * 1000UL;
  syncOutputs();
}

void factoryReset() {
  if (LittleFS.exists("/config.txt")) LittleFS.remove("/config.txt");
  delay(300);
  ESP.restart();
}

String htmlPage() {
  String s;
  s.reserve(9000);
  s += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>";
  s += "<title>CholoBiddut</title>";
  s += "<style>";
  s += "body{margin:0;font-family:Arial,sans-serif;background:#f4f7fb;color:#111}";
  s += ".wrap{max-width:560px;margin:0 auto;padding:14px 14px 70px}";
  s += ".card{background:#fff;border-radius:18px;padding:14px;box-shadow:0 8px 22px rgba(0,0,0,.08);margin-bottom:12px}";
  s += ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}";
  s += ".btn{width:100%;padding:18px 12px;border:0;border-radius:16px;font-size:18px;font-weight:700}";
  s += ".on{background:#16a34a;color:#fff}.off{background:#dc2626;color:#fff}.gray{background:#334155;color:#fff}.blue{background:#2563eb;color:#fff}";
  s += ".top{display:flex;justify-content:space-between;align-items:center;gap:10px}";
  s += ".iconbtn{width:44px;height:44px;border-radius:14px;border:0;background:#e2e8f0;font-size:20px}";
  s += ".footer{text-align:center;color:#64748b;font-size:11px;position:fixed;left:0;right:0;bottom:0;padding:10px 12px;background:linear-gradient(to top,#f4f7fb 70%,rgba(244,247,251,0))}";
  s += ".small{font-size:13px;color:#475569;line-height:1.45}";
  s += ".hidden{display:none}";
  s += ".input{width:100%;padding:14px;border:1px solid #dbe2ea;border-radius:14px;font-size:16px;box-sizing:border-box;margin-top:8px}";
  s += ".label{font-size:13px;color:#334155;margin-top:10px;display:block}";
  s += ".settings{margin-top:10px;padding:12px;border-radius:14px;background:#f8fafc;border:1px solid #e2e8f0}";
  s += ".timer{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}";
  s += ".chip{padding:10px 12px;border-radius:999px;background:#e2e8f0;border:0;font-weight:700}";
  s += ".count{margin-top:8px;font-size:13px;color:#0f172a}";
  s += "</style></head><body><div class='wrap'>";

  s += "<div class='card'><div class='top'><div><div style='font-size:20px;font-weight:800'>Home Control</div><div class='small'>AP: <span id='ssid'>...</span> · IP: <span id='ip'>...</span></div></div><button class='iconbtn' onclick='toggleSettings()'>⚙️</button></div>";
  s += "<div id='settingsBox' class='settings hidden'>";
  s += "<div class='small'>Press and hold the configure button for 5 seconds to factory reset.</div>";
  s += "<div class='label'>AP SSID</div><input id='apSsid' class='input'>";
  s += "<div class='label'>AP Password</div><input id='apPass' class='input' type='password'>";
  s += "<div class='label'>Switch 1 Name</div><input id='sw1Name' class='input'>";
  s += "<div class='label'>Switch 2 Name</div><input id='sw2Name' class='input'>";
  s += "<div class='label'>mDNS Name</div><input id='mdnsName' class='input'>";
  s += "<div class='label'>Default Timer Seconds</div><input id='timerSec' class='input' type='number' min='1' value='300'>";
  s += "<button class='btn blue' style='margin-top:12px' onclick='saveSettings()'>Save Settings</button>";
  s += "</div></div>";

  s += "<div class='card'><div class='row'>";
  s += "<div><button id='b1' class='btn off' onclick='toggleRelay(1)'>Switch 1 OFF</button><div class='count'>Timer S1: <span id='c1'>0</span>s</div>";
  s += "<div class='timer'><button class='chip' onclick='setTimer(1,60)'>+1 min</button><button class='chip' onclick='setTimer(1,300)'>+5 min</button><button class='chip' onclick='setTimer(1,customTimer())'>Custom</button></div></div>";
  s += "<div><button id='b2' class='btn off' onclick='toggleRelay(2)'>Switch 2 OFF</button><div class='count'>Timer S2: <span id='c2'>0</span>s</div>";
  s += "<div class='timer'><button class='chip' onclick='setTimer(2,60)'>+1 min</button><button class='chip' onclick='setTimer(2,300)'>+5 min</button><button class='chip' onclick='setTimer(2,customTimer())'>Custom</button></div></div>";
  s += "</div></div>";

  s += "<div class='card'><div class='small'>Custom Timer Seconds: <input id='customTimerVal' class='input' type='number' min='1' value='300' style='margin-top:8px'></div></div>";
  s += "<div class='card'><button class='btn gray' onclick='toggleSettings()'>Settings</button></div>";
  s += "</div><div class='footer'>Powered by CholoBiddut © 2026 CholoBiddut. All rights reserved.</div>";

  s += "<script>";
  s += "let ws;function connect(){ws=new WebSocket('ws://'+location.host+':81/');ws.onopen=()=>ws.send('get');";
  s += "ws.onmessage=e=>{let d=JSON.parse(e.data);";
  s += "ssid.textContent=d.ssid;ip.textContent=d.ip;apSsid.value=d.ssid;sw1Name.value=d.n1;sw2Name.value=d.n2;mdnsName.value=d.mdns;timerSec.value=d.d;";
  s += "upd(1,d.r1,d.n1);upd(2,d.r2,d.n2);c1.textContent=d.c1;c2.textContent=d.c2;};";
  s += "ws.onclose=()=>setTimeout(connect,700)}connect();";
  s += "function upd(n,v,name){let b=document.getElementById('b'+n);b.textContent=(name||('Switch '+n))+' '+(v?'ON':'OFF');b.className='btn '+(v?'on':'off');}";
  s += "function toggleRelay(n){if(ws&&ws.readyState===1)ws.send('toggle:'+n)}";
  s += "function customTimer(){return parseInt(document.getElementById('customTimerVal').value||0)}";
  s += "function setTimer(n,sec){sec=parseInt(sec||0);if(ws&&ws.readyState===1&&sec>0)ws.send('timer:'+n+':'+sec)}";
  s += "function toggleSettings(){settingsBox.classList.toggle('hidden')}";
  s += "function saveSettings(){if(ws&&ws.readyState===1){ws.send('save:'+apSsid.value+'|'+apPass.value+'|'+sw1Name.value+'|'+sw2Name.value+'|'+mdnsName.value+'|'+timerSec.value);}}";
  s += "</script></body></html>";
  return s;
}

void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleState() {
  sendState();
  server.send(200, "application/json", "{\"ok\":true}");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type != WStype_TEXT) return;
  String msg = String((char*)payload);

  if (msg == "get") {
    sendState();
  } else if (msg == "toggle:1") {
    toggleRelay1();
  } else if (msg == "toggle:2") {
    toggleRelay2();
  } else if (msg.startsWith("timer:")) {
    int p1 = msg.indexOf(':');
    int p2 = msg.indexOf(':', p1 + 1);
    int ch = msg.substring(p1 + 1, p2).toInt();
    uint32_t sec = msg.substring(p2 + 1).toInt();
    if (sec == 0) sec = cfg.defaultTimerSec;
    if (ch == 1) startTimer1(sec);
    if (ch == 2) startTimer2(sec);
  } else if (msg.startsWith("save:")) {
    String rest = msg.substring(5);
    int a = rest.indexOf('|');
    int b = rest.indexOf('|', a + 1);
    int c = rest.indexOf('|', b + 1);
    int d = rest.indexOf('|', c + 1);
    int e = rest.indexOf('|', d + 1);

    if (a > 0 && b > a && c > b && d > c && e > d) {
      cfg.apSsid = rest.substring(0, a);
      cfg.apPass = rest.substring(a + 1, b);
      cfg.sw1Name = rest.substring(b + 1, c);
      cfg.sw2Name = rest.substring(c + 1, d);
      cfg.mdnsName = rest.substring(d + 1, e);
      cfg.defaultTimerSec = rest.substring(e + 1).toInt();

      if (cfg.apSsid.length() == 0) cfg.apSsid = "CholoBiddut Smart Home";
      if (cfg.apPass.length() < 8) cfg.apPass = "12345678";
      if (cfg.mdnsName.length() == 0) cfg.mdnsName = "cholobiddut";
      if (cfg.defaultTimerSec == 0) cfg.defaultTimerSec = 300;

      saveSettings();
      webSocket.broadcastTXT("{\"reboot\":true}");
      delay(300);
      ESP.restart();
    }
  }
}

void startConfigMode() {
  factoryReset();
}

void blinkLedFast() {
  unsigned long now = millis();
  if (now - ledLastToggle >= 200) {
    ledLastToggle = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
}

void blinkLedSlow() {
  unsigned long now = millis();
  if (now - ledLastToggle >= 2000) {
    ledLastToggle = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
}

void updateLed() {
  blinkLedSlow();
}

void checkTimers() {
  unsigned long now = millis();
  if (relay1TimerActive && (long)(now - relay1TimerEnd) >= 0) {
    relay1TimerActive = false;
    relay1State = false;
    syncOutputs();
  }
  if (relay2TimerActive && (long)(now - relay2TimerEnd) >= 0) {
    relay2TimerActive = false;
    relay2State = false;
    syncOutputs();
  }
}

void handleButtons() {
  unsigned long now = millis();

  bool b1 = digitalRead(BTN1_PIN);
  bool b2 = digitalRead(BTN2_PIN);
  bool cfgb = digitalRead(CFG_BTN_PIN);

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

  if (cfgb != cfgBtnLast && (now - lastCfgChange) > DEBOUNCE_MS) {
    lastCfgChange = now;
    cfgBtnLast = cfgb;
    if (cfgb == LOW) {
      cfgHoldStart = now;
      cfgHoldActive = true;
      cfgTriggered = false;
    } else {
      cfgHoldActive = false;
    }
  }

  if (cfgHoldActive && !cfgTriggered && cfgb == LOW && (now - cfgHoldStart) >= CFG_HOLD_MS) {
    cfgTriggered = true;
    startConfigMode();
  }
}

void setup() {
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(CFG_BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  applyRelay(RELAY1_PIN, false);
  applyRelay(RELAY2_PIN, false);

  Serial.begin(115200);
  LittleFS.begin();
  loadSettings();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(cfg.apSsid.c_str(), cfg.apPass.c_str());

  MDNS.begin(cfg.mdnsName.c_str());
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);

  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  syncOutputs();
}

void loop() {
  server.handleClient();
  webSocket.loop();
  MDNS.update();
  handleButtons();
  checkTimers();
  updateLed();
}