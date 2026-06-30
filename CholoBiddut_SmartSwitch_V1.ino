/*
   =================================================================
   Project: CholoBiddut Smart Switch (ESP8266) - FIXED VERSION
   =================================================================
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>

// ==================== PIN DEFINITIONS ====================
#define RELAY1_PIN   D1
#define RELAY2_PIN   D2
#define BUTTON1_PIN  D5
#define BUTTON2_PIN  D6
#define AP_BUTTON_PIN D7
#define STATUS_LED_PIN D0

// ==================== CONSTANTS ====================
#define AP_SSID "CholoBiddut-Setup"
#define AP_PASS "12345678"
#define MDNS_NAME "cholobiddut"
#define LONG_PRESS_TIME 5000
#define DEBOUNCE_DELAY 50

// ==================== GLOBAL VARIABLES ====================
ESP8266WebServer server(80);
DNSServer dnsServer;

bool load1State = false;
bool load2State = false;
bool lastLoad1State = false;
bool lastLoad2State = false;

bool forceAP = false;
bool apModeActive = false;
bool isConnected = false;
bool wifiConfigDone = false;

unsigned long apButtonPressTime = 0;
bool apButtonPressed = false;
unsigned long lastBlinkTime = 0;
bool ledState = false;
unsigned long lastReconnectAttempt = 0;

struct WiFiCreds {
  char ssid[32];
  char pass[64];
  bool valid;
} wifiCreds;

// ==================== EEPROM FUNCTIONS ====================
void loadWiFiCreds() {
  EEPROM.begin(512);
  EEPROM.get(0, wifiCreds);
  EEPROM.end();
}

void saveWiFiCreds() {
  EEPROM.begin(512);
  EEPROM.put(0, wifiCreds);
  EEPROM.commit();
  EEPROM.end();
}

void clearWiFiCreds() {
  wifiCreds.valid = false;
  wifiCreds.ssid[0] = '\0';
  wifiCreds.pass[0] = '\0';
  saveWiFiCreds();
}

// ==================== RELAY CONTROL ====================
void setLoad1(bool state) {
  load1State = state;
  digitalWrite(RELAY1_PIN, state ? LOW : HIGH);
  lastLoad1State = state;
}

void setLoad2(bool state) {
  load2State = state;
  digitalWrite(RELAY2_PIN, state ? LOW : HIGH);
  lastLoad2State = state;
}

void toggleLoad1() {
  setLoad1(!load1State);
}

void toggleLoad2() {
  setLoad2(!load2State);
}

// ==================== LED INDICATOR ====================
void updateLED() {
  unsigned long currentTime = millis();
  int blinkInterval = isConnected ? 1000 : 200;
  
  if (currentTime - lastBlinkTime >= blinkInterval) {
    lastBlinkTime = currentTime;
    ledState = !ledState;
    digitalWrite(STATUS_LED_PIN, ledState);
  }
}

// ==================== PHYSICAL BUTTON HANDLING ====================
void handleButtons() {
  static unsigned long lastDebounce1 = 0;
  static unsigned long lastDebounce2 = 0;
  static bool lastButton1State = HIGH;
  static bool lastButton2State = HIGH;
  
  bool reading1 = digitalRead(BUTTON1_PIN);
  if (reading1 != lastButton1State) {
    lastDebounce1 = millis();
  }
  if ((millis() - lastDebounce1) > DEBOUNCE_DELAY) {
    if (reading1 == LOW && lastButton1State == HIGH) {
      toggleLoad1();
    }
  }
  lastButton1State = reading1;
  
  bool reading2 = digitalRead(BUTTON2_PIN);
  if (reading2 != lastButton2State) {
    lastDebounce2 = millis();
  }
  if ((millis() - lastDebounce2) > DEBOUNCE_DELAY) {
    if (reading2 == LOW && lastButton2State == HIGH) {
      toggleLoad2();
    }
  }
  lastButton2State = reading2;
  
  bool apReading = digitalRead(AP_BUTTON_PIN);
  if (apReading == LOW) {
    if (!apButtonPressed) {
      apButtonPressed = true;
      apButtonPressTime = millis();
    }
  } else {
    if (apButtonPressed) {
      if (millis() - apButtonPressTime >= LONG_PRESS_TIME) {
        forceAP = true;
        wifiConfigDone = false;
        clearWiFiCreds();
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        apModeActive = true;
        isConnected = false;
        Serial.println("Forced AP mode via button");
      }
      apButtonPressed = false;
    }
  }
}

// ==================== WEB SERVER HANDLERS ====================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="bn">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>CholoBiddut Smart Switch</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Arial, sans-serif;
        }
        body {
            background: #f0f2f5;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            max-width: 400px;
            width: 100%;
            background: white;
            border-radius: 24px;
            padding: 30px 25px;
            box-shadow: 0 8px 30px rgba(0,0,0,0.12);
            margin: 20px 0;
        }
        .header {
            text-align: center;
            margin-bottom: 30px;
        }
        .header h1 {
            color: #1a1a2e;
            font-size: 26px;
            font-weight: 700;
        }
        .header .subtitle {
            color: #6c757d;
            font-size: 14px;
            margin-top: 5px;
        }
        .status-badge {
            display: inline-block;
            padding: 4px 14px;
            border-radius: 20px;
            font-size: 12px;
            font-weight: 600;
            margin-top: 8px;
        }
        .status-connected {
            background: #d4edda;
            color: #155724;
        }
        .status-disconnected {
            background: #f8d7da;
            color: #721c24;
        }
        .load-card {
            background: #f8f9fa;
            border-radius: 16px;
            padding: 18px 20px;
            margin-bottom: 16px;
            display: flex;
            align-items: center;
            justify-content: space-between;
            transition: all 0.3s ease;
            border: 2px solid transparent;
        }
        .load-card.active {
            border-color: #28a745;
            background: #f0fff4;
        }
        .load-card.inactive {
            border-color: #dc3545;
            background: #fff5f5;
        }
        .load-info {
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
        .load-name {
            font-size: 18px;
            font-weight: 600;
            color: #1a1a2e;
        }
        .load-status-text {
            font-size: 13px;
            font-weight: 500;
        }
        .status-on { color: #28a745; }
        .status-off { color: #dc3545; }
        .btn-toggle {
            width: 70px;
            height: 70px;
            border-radius: 50%;
            border: none;
            font-size: 28px;
            font-weight: 700;
            cursor: pointer;
            transition: all 0.2s ease;
            box-shadow: 0 4px 12px rgba(0,0,0,0.15);
            touch-action: manipulation;
            background: #e9ecef;
            color: #495057;
        }
        .btn-toggle.on {
            background: #28a745;
            color: white;
            box-shadow: 0 4px 16px rgba(40, 167, 69, 0.4);
        }
        .btn-toggle.off {
            background: #dc3545;
            color: white;
            box-shadow: 0 4px 16px rgba(220, 53, 69, 0.4);
        }
        .btn-toggle:active {
            transform: scale(0.92);
        }
        .footer {
            text-align: center;
            padding: 15px 0 5px 0;
            width: 100%;
            max-width: 400px;
        }
        .footer p {
            color: #6c757d;
            font-size: 12px;
            margin: 2px 0;
        }
        .footer .powered {
            font-weight: 500;
            color: #495057;
        }
        .footer .copyright {
            font-size: 11px;
            color: #adb5bd;
        }
        .wifi-section {
            margin-top: 20px;
            padding-top: 16px;
            border-top: 1px solid #e9ecef;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .wifi-info {
            font-size: 13px;
            color: #6c757d;
        }
        .btn-reboot {
            background: #6c757d;
            color: white;
            border: none;
            padding: 8px 20px;
            border-radius: 20px;
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
            transition: background 0.2s;
        }
        .btn-reboot:active {
            background: #5a6268;
        }
        .alert {
            background: #fff3cd;
            color: #856404;
            padding: 12px 16px;
            border-radius: 12px;
            font-size: 14px;
            margin-bottom: 16px;
            border-left: 4px solid #ffc107;
        }
        .config-section {
            margin-top: 15px;
            padding-top: 15px;
            border-top: 1px solid #e9ecef;
            text-align: center;
        }
        .config-section small {
            color: #6c757d;
            font-size: 12px;
        }
        @media (max-width: 480px) {
            .container { padding: 20px 16px; }
            .load-card { padding: 14px 16px; }
            .btn-toggle { width: 60px; height: 60px; font-size: 24px; }
            .header h1 { font-size: 22px; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>CholoBiddut</h1>
            <div class="subtitle">Smart Switch Control</div>
            <span class="status-badge" id="statusBadge">Connecting...</span>
        </div>
        
        <div id="alertBox" style="display:none;" class="alert">Failed to toggle. Please try again.</div>
        
        <div id="load1Card" class="load-card">
            <div class="load-info">
                <span class="load-name">Light</span>
                <span class="load-status-text" id="load1Status">Off</span>
            </div>
            <button class="btn-toggle" id="btn1" onclick="toggleLoad(1)">OFF</button>
        </div>
        
        <div id="load2Card" class="load-card">
            <div class="load-info">
                <span class="load-name">Fan</span>
                <span class="load-status-text" id="load2Status">Off</span>
            </div>
            <button class="btn-toggle" id="btn2" onclick="toggleLoad(2)">OFF</button>
        </div>
        
        <div class="wifi-section">
            <span class="wifi-info" id="wifiInfo">Not connected</span>
            <button class="btn-reboot" onclick="rebootESP()">Reboot</button>
        </div>
        
        <div class="config-section">
            <small>Press D7 button for 5 seconds to reconfigure WiFi</small>
        </div>
    </div>
    
    <div class="footer">
        <p class="powered">Powered by CholoBiddut</p>
        <p class="copyright">© 2026 CholoBiddut. All rights reserved.</p>
    </div>
    
    <script>
        function fetchStatus() {
            fetch('/status')
                .then(res => res.json())
                .then(data => {
                    updateLoadUI(1, data.load1);
                    updateLoadUI(2, data.load2);
                    document.getElementById('wifiInfo').innerHTML = data.wifiSSID;
                    const badge = document.getElementById('statusBadge');
                    if (data.wifiConnected) {
                        badge.textContent = 'Connected';
                        badge.className = 'status-badge status-connected';
                    } else {
                        badge.textContent = 'AP Mode';
                        badge.className = 'status-badge status-disconnected';
                    }
                })
                .catch(() => {});
        }
        
        function updateLoadUI(load, state) {
            const statusText = state ? 'On' : 'Off';
            const statusClass = state ? 'status-on' : 'status-off';
            const btn = document.getElementById('btn' + load);
            const card = document.getElementById('load' + load + 'Card');
            const statusSpan = document.getElementById('load' + load + 'Status');
            
            btn.textContent = state ? 'ON' : 'OFF';
            btn.className = 'btn-toggle ' + (state ? 'on' : 'off');
            card.className = 'load-card ' + (state ? 'active' : 'inactive');
            statusSpan.textContent = statusText;
            statusSpan.className = 'load-status-text ' + statusClass;
        }
        
        function toggleLoad(load) {
            const btn = document.getElementById('btn' + load);
            btn.disabled = true;
            btn.style.opacity = '0.7';
            
            fetch('/toggle?load=' + load)
                .then(res => res.json())
                .then(data => {
                    if (data.success) {
                        updateLoadUI(load, data.state);
                        document.getElementById('alertBox').style.display = 'none';
                    } else {
                        document.getElementById('alertBox').style.display = 'block';
                    }
                })
                .catch(() => {
                    document.getElementById('alertBox').style.display = 'block';
                })
                .finally(() => {
                    btn.disabled = false;
                    btn.style.opacity = '1';
                });
        }
        
        function rebootESP() {
            if (confirm('Reboot the device?')) {
                fetch('/reboot')
                    .then(() => {
                        document.getElementById('statusBadge').textContent = 'Rebooting...';
                        setTimeout(() => location.reload(), 5000);
                    });
            }
        }
        
        fetchStatus();
        setInterval(fetchStatus, 2000);
    </script>
</body>
</html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleStatus() {
  String json = "{";
  json += "\"load1\":" + String(load1State ? "true" : "false") + ",";
  json += "\"load2\":" + String(load2State ? "true" : "false") + ",";
  json += "\"wifiConnected\":" + String(isConnected ? "true" : "false") + ",";
  json += "\"wifiSSID\":\"" + String(isConnected ? WiFi.SSID() : "AP Mode") + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleToggle() {
  if (server.hasArg("load")) {
    int load = server.arg("load").toInt();
    bool success = false;
    bool currentState = false;
    
    if (load == 1) {
      toggleLoad1();
      currentState = load1State;
      success = true;
    } else if (load == 2) {
      toggleLoad2();
      currentState = load2State;
      success = true;
    }
    
    String json = "{";
    json += "\"success\":" + String(success ? "true" : "false") + ",";
    json += "\"state\":" + String(currentState ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing load parameter\"}");
  }
}

void handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(200);
  ESP.restart();
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

void handleConfig() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiFi Setup</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; font-family: Arial, sans-serif; }
        body { background: #f0f2f5; display: flex; justify-content: center; align-items: center; min-height: 100vh; padding: 20px; }
        .container { background: white; padding: 30px; border-radius: 16px; max-width: 400px; width: 100%; box-shadow: 0 4px 20px rgba(0,0,0,0.1); }
        h1 { text-align: center; color: #1a1a2e; margin-bottom: 20px; font-size: 24px; }
        label { display: block; margin: 15px 0 5px; font-weight: 600; color: #333; }
        select, input { width: 100%; padding: 12px; border: 2px solid #ddd; border-radius: 8px; font-size: 16px; }
        select:focus, input:focus { border-color: #007bff; outline: none; }
        button { width: 100%; padding: 14px; background: #28a745; color: white; border: none; border-radius: 8px; font-size: 18px; font-weight: 600; cursor: pointer; margin-top: 20px; }
        button:active { background: #218838; }
        .scan-btn { background: #007bff; margin-top: 10px; }
        .scan-btn:active { background: #0069d9; }
        #status { margin-top: 15px; padding: 10px; border-radius: 8px; text-align: center; display: none; }
        .success { background: #d4edda; color: #155724; display: block; }
        .error { background: #f8d7da; color: #721c24; display: block; }
        .info { background: #cce5ff; color: #004085; display: block; }
    </style>
</head>
<body>
    <div class="container">
        <h1>WiFi Configuration</h1>
        <form id="configForm">
            <label for="ssid">Select WiFi Network:</label>
            <select id="ssid" name="ssid" required>
                <option value="">-- Select Network --</option>
            </select>
            <button type="button" class="scan-btn" onclick="scanNetworks()">Scan Networks</button>
            
            <label for="password">Password:</label>
            <input type="password" id="password" name="password" placeholder="Enter WiFi password">
            
            <button type="submit">Connect</button>
        </form>
        <div id="status"></div>
    </div>
    
    <script>
        function scanNetworks() {
            const status = document.getElementById('status');
            status.className = 'info';
            status.textContent = 'Scanning networks...';
            status.style.display = 'block';
            
            fetch('/scan')
                .then(res => res.json())
                .then(data => {
                    const select = document.getElementById('ssid');
                    select.innerHTML = '<option value="">-- Select Network --</option>';
                    data.networks.forEach(net => {
                        const option = document.createElement('option');
                        option.value = net;
                        option.textContent = net;
                        select.appendChild(option);
                    });
                    status.className = 'success';
                    status.textContent = 'Scan complete. Select your network.';
                })
                .catch(() => {
                    status.className = 'error';
                    status.textContent = 'Failed to scan networks.';
                });
        }
        
        document.getElementById('configForm').onsubmit = function(e) {
            e.preventDefault();
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            const status = document.getElementById('status');
            
            if (!ssid) {
                status.className = 'error';
                status.textContent = 'Please select a network.';
                status.style.display = 'block';
                return;
            }
            
            status.className = 'info';
            status.textContent = 'Connecting to ' + ssid + '...';
            status.style.display = 'block';
            
            fetch('/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(password)
            })
            .then(res => res.json())
            .then(data => {
                if (data.success) {
                    status.className = 'success';
                    status.textContent = 'Connected successfully! Rebooting...';
                    setTimeout(() => { window.location.href = '/'; }, 3000);
                } else {
                    status.className = 'error';
                    status.textContent = 'Connection failed: ' + data.error;
                }
            })
            .catch(() => {
                status.className = 'error';
                status.textContent = 'Failed to connect. Please try again.';
            });
        };
        
        // Auto-scan on page load
        scanNetworks();
    </script>
</body>
</html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleScan() {
  String json = "{\"networks\":[";
  int n = WiFi.scanComplete();
  if (n == -2) {
    WiFi.scanNetworks(true);
    json += "]}";
    server.send(200, "application/json", json);
    return;
  } else if (n > 0) {
    for (int i = 0; i < n; ++i) {
      if (i) json += ",";
      json += "\"" + WiFi.SSID(i) + "\"";
    }
    WiFi.scanDelete();
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    
    ssid.toCharArray(wifiCreds.ssid, sizeof(wifiCreds.ssid));
    pass.toCharArray(wifiCreds.pass, sizeof(wifiCreds.pass));
    wifiCreds.valid = true;
    saveWiFiCreds();
    
    String json = "{\"success\":true}";
    server.send(200, "application/json", json);
    
    delay(500);
    ESP.restart();
  } else {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing parameters\"}");
  }
}

// ==================== WIFI SETUP ====================
void startAPMode() {
  apModeActive = true;
  isConnected = false;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  MDNS.begin(MDNS_NAME);
  MDNS.addService("http", "tcp", 80);
  
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/toggle", handleToggle);
  server.on("/reboot", handleReboot);
  server.on("/config", handleConfig);
  server.on("/scan", handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  
  Serial.println("\nAP Mode Active");
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
  Serial.print("SSID: "); Serial.println(AP_SSID);
  Serial.print("Password: "); Serial.println(AP_PASS);
}

bool connectToWiFi() {
  if (!wifiCreds.valid) {
    return false;
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiCreds.ssid, wifiCreds.pass);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    isConnected = true;
    apModeActive = false;
    wifiConfigDone = true;
    return true;
  }
  return false;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\nCholoBiddut Smart Switch");
  
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(AP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  digitalWrite(STATUS_LED_PIN, LOW);
  
  loadWiFiCreds();
  
  if (digitalRead(AP_BUTTON_PIN) == LOW) {
    unsigned long startTime = millis();
    while (digitalRead(AP_BUTTON_PIN) == LOW) {
      if (millis() - startTime >= LONG_PRESS_TIME) {
        forceAP = true;
        clearWiFiCreds();
        break;
      }
      delay(10);
    }
  }
  
  if (!forceAP && wifiCreds.valid && connectToWiFi()) {
    MDNS.begin(MDNS_NAME);
    MDNS.addService("http", "tcp", 80);
    
    server.on("/", handleRoot);
    server.on("/status", handleStatus);
    server.on("/toggle", handleToggle);
    server.on("/reboot", handleReboot);
    server.onNotFound(handleNotFound);
    server.begin();
    
    Serial.println("Connected to WiFi");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    Serial.print("mDNS: http://"); Serial.print(MDNS_NAME); Serial.println(".local");
  } else {
    forceAP = false;
    startAPMode();
  }
}

// ==================== LOOP ====================
void loop() {
  server.handleClient();
  
  if (apModeActive) {
    dnsServer.processNextRequest();
  }
  
  if (isConnected) {
    MDNS.update();
  }
  
  handleButtons();
  updateLED();
  
  if (!apModeActive && WiFi.status() != WL_CONNECTED) {
    if (millis() - lastReconnectAttempt > 10000) {
      lastReconnectAttempt = millis();
      if (wifiCreds.valid && connectToWiFi()) {
        MDNS.begin(MDNS_NAME);
        MDNS.addService("http", "tcp", 80);
      } else {
        startAPMode();
      }
    }
  }
  
  delay(10);
}