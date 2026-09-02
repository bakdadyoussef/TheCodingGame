#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

#define DHTPIN 6
#define DHTTYPE DHT11
#define RELAY_PIN 4

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

const char* ssid = "fh_hp2";
const char* password = "kingkingkingking";

bool relayState = false;
unsigned long relayOffTime = 0;

// New features
float tempThreshold = 28.0;
bool autoTempMode = false;
bool timerEnabled = false;

float currentTemp = 0.0;
float currentHumidity = 0.0;
unsigned long lastSensorRead = 0;

void relayON() {
  digitalWrite(RELAY_PIN, HIGH);
  relayState = true;
}

void relayOFF() {
  digitalWrite(RELAY_PIN, LOW);
  relayState = false;
  relayOffTime = 0;
}

String formatTime(unsigned long seconds) {
  unsigned long h = seconds / 3600;
  unsigned long m = (seconds % 3600) / 60;
  unsigned long s = seconds % 60;
  char buf[20];
  sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

// JSON data endpoint
void handleData() {
  unsigned long now = millis();
  
  // Force sensor read on every data request for more responsive UI
  currentTemp = dht.readTemperature();
  currentHumidity = dht.readHumidity();
  
  if (isnan(currentTemp)) currentTemp = 0;
  if (isnan(currentHumidity)) currentHumidity = 0;

  long remaining = 0;
  if (relayState && relayOffTime > 0) {
    remaining = (relayOffTime - millis()) / 1000;
    if (remaining < 0) remaining = 0;
  }

  String json = "{";
  json += "\"temperature\":" + String(currentTemp, 1) + ",";
  json += "\"humidity\":" + String(currentHumidity, 1) + ",";
  json += "\"relay\":" + String(relayState ? "true" : "false") + ",";
  json += "\"remaining\":" + String(remaining) + ",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"tempThreshold\":" + String(tempThreshold, 1) + ",";
  json += "\"autoTempMode\":" + String(autoTempMode ? "true" : "false") + ",";
  json += "\"timerEnabled\":" + String(timerEnabled ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

// Main page with improved modern UI
void handleRoot() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Smart Relay</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap');
        
        :root {
            --primary: #3b82f6;
            --success: #22c55e;
            --danger: #ef4444;
        }
        
        * { margin: 0; padding: 0; box-sizing: border-box; }
        
        body {
            font-family: 'Inter', system-ui, sans-serif;
            background: linear-gradient(135deg, #f1f5f9 0%, #e0e7ff 100%);
            min-height: 100vh;
            color: #1e2937;
        }
        
        .container { max-width: 680px; margin: 0 auto; padding: 20px; }
        
        header { text-align: center; margin-bottom: 32px; }
        
        h1 {
            font-size: 2.4rem;
            font-weight: 700;
            background: linear-gradient(to right, #1e40af, #3b82f6);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        
        .card {
            background: white;
            border-radius: 24px;
            box-shadow: 0 20px 25px -5px rgb(0 0 0 / 0.1);
            padding: 28px;
            margin-bottom: 24px;
        }
        
        .sensor-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
        }
        
        .sensor {
            text-align: center;
            padding: 24px 16px;
            border-radius: 20px;
            background: #f8fafc;
        }
        
        .sensor-value {
            font-size: 3rem;
            font-weight: 700;
            line-height: 1;
            margin-bottom: 4px;
        }
        
        .toggle-container {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin: 20px 0;
        }
        
        .toggle {
            position: relative;
            display: inline-block;
            width: 72px;
            height: 38px;
        }
        
        .toggle input { opacity: 0; width: 0; height: 0; }
        
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0; left: 0; right: 0; bottom: 0;
            background-color: #cbd5e1;
            transition: .4s;
            border-radius: 34px;
        }
        
        .slider:before {
            position: absolute;
            content: "";
            height: 30px; width: 30px;
            left: 4px; bottom: 4px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }
        
        input:checked + .slider { background-color: var(--success); }
        input:checked + .slider:before { transform: translateX(34px); }
        
        .status {
            font-size: 1.25rem;
            font-weight: 600;
            padding: 10px 24px;
            border-radius: 9999px;
            display: inline-block;
        }
        
        .btn {
            padding: 14px 32px;
            font-size: 1.1rem;
            font-weight: 600;
            border: none;
            border-radius: 14px;
            cursor: pointer;
            transition: all 0.3s;
            width: 100%;
            margin-top: 12px;
        }
        
        .btn-primary { background: var(--primary); color: white; }
        .btn-primary:hover { background: #2563eb; transform: translateY(-2px); }
        .btn-danger { background: var(--danger); color: white; }
        
        input[type="number"] {
            padding: 14px;
            font-size: 1.1rem;
            border: 2px solid #e2e8f0;
            border-radius: 12px;
            width: 140px;
            text-align: center;
        }
        
        .control-row {
            display: flex;
            align-items: center;
            gap: 16px;
            margin: 16px 0;
        }
        
        .remaining {
            font-size: 1.6rem;
            font-weight: 700;
            color: #1e40af;
            text-align: center;
            margin: 20px 0;
            padding: 16px;
            background: #f0f9ff;
            border-radius: 16px;
        }
        
        footer {
            text-align: center;
            margin-top: 40px;
            color: #64748b;
            font-size: 0.95rem;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>ESP32 Smart Relay</h1>
            <p style="color:#64748b; font-size:1.1rem;">Temperature • Timer • Live Control</p>
        </header>
        
        <!-- Sensors -->
        <div class="card">
            <h2 style="text-align:center; color:#1e40af; margin-bottom:20px;">Environmental Sensors</h2>
            <div class="sensor-grid">
                <div class="sensor">
                    <div style="font-size:0.95rem; color:#64748b; margin-bottom:12px;">🌡️ Temperature</div>
                    <div class="sensor-value" id="temp">0.0</div>
                    <span style="font-size:1.2rem; color:#64748b;">°C</span>
                </div>
                <div class="sensor">
                    <div style="font-size:0.95rem; color:#64748b; margin-bottom:12px;">💧 Humidity</div>
                    <div class="sensor-value" id="hum">0.0</div>
                    <span style="font-size:1.2rem; color:#64748b;">%</span>
                </div>
            </div>
        </div>
        
        <!-- Relay Control -->
        <div class="card">
            <h2 style="text-align:center; color:#1e40af; margin-bottom:20px;">Manual Relay Control</h2>
            <div class="toggle-container">
                <div><strong style="font-size:1.4rem;">Relay Status</strong></div>
                <label class="toggle">
                    <input type="checkbox" id="relayToggle" onchange="toggleRelay()">
                    <span class="slider"></span>
                </label>
            </div>
            <div id="relayStatus" class="status" style="background:#f1f5f9; color:#475569;">OFF</div>
        </div>
        
        <!-- Auto Temperature Control -->
        <div class="card">
            <h2 style="text-align:center; color:#1e40af; margin-bottom:20px;">🔥 Auto Temperature Control</h2>
            <div class="control-row">
                <label class="toggle" style="margin-right:12px;">
                    <input type="checkbox" id="autoTempToggle" onchange="toggleAutoTemp()">
                    <span class="slider"></span>
                </label>
                <div><strong>Enable Auto-ON when temperature reaches</strong></div>
            </div>
            
            <div style="display:flex; gap:12px; align-items:center; margin-top:16px;">
                <input type="number" id="tempThreshold" value="28" step="0.5" min="10" max="60" style="width:120px;">
                <span style="font-size:1.1rem; color:#64748b;">°C</span>
                <button class="btn btn-primary" onclick="setTempThreshold()" style="width:auto; flex:1;">Save Threshold</button>
            </div>
        </div>
        
        <!-- Timer -->
        <div class="card">
            <h2 style="text-align:center; color:#1e40af; margin-bottom:20px;">⏱️ Timed Operation</h2>
            <div class="control-row">
                <label class="toggle">
                    <input type="checkbox" id="timerEnabledToggle" onchange="toggleTimerMode()">
                    <span class="slider"></span>
                </label>
                <strong>Enable Timer Mode</strong>
            </div>
            
            <div id="timerInputs" style="display:none;">
                <div style="display:flex; gap:12px; align-items:center; margin-top:16px;">
                    <input type="number" id="minutes" min="1" max="1440" value="30">
                    <button class="btn btn-primary" onclick="startTimer()" style="width:auto; flex:1;">Start Timer (ON)</button>
                </div>
            </div>
            
            <div id="remainingContainer" style="display:none; margin-top:20px;">
                <div class="remaining">Remaining: <span id="remainingTime">00:00:00</span></div>
                <button onclick="cancelTimer()" class="btn btn-danger">Cancel Timer</button>
            </div>
        </div>
        
        <!-- Uptime -->
        <div class="card" style="text-align:center;">
            <h3 style="color:#64748b; margin-bottom:12px;">Device Uptime</h3>
            <div id="uptime" style="font-size:1.8rem; font-weight:700; color:#334155;">00:00:00</div>
        </div>
    </div>
    
    <footer>ESP32 • DHT11 • Auto Temp + Timer Control</footer>

    <script>
        async function fetchData() {
            try {
                const response = await fetch('/data');
                const data = await response.json();
                
                document.getElementById('temp').textContent = data.temperature.toFixed(1);
                document.getElementById('hum').textContent = data.humidity.toFixed(1);
                
                // Relay status
                const toggle = document.getElementById('relayToggle');
                toggle.checked = data.relay;
                
                const statusEl = document.getElementById('relayStatus');
                if (data.relay) {
                    statusEl.textContent = 'ON';
                    statusEl.style.background = '#dcfce7';
                    statusEl.style.color = '#166534';
                } else {
                    statusEl.textContent = 'OFF';
                    statusEl.style.background = '#f1f5f9';
                    statusEl.style.color = '#475569';
                }
                
                document.getElementById('uptime').textContent = formatTime(data.uptime);
                
                // Remaining timer
                const remContainer = document.getElementById('remainingContainer');
                if (data.remaining > 0) {
                    remContainer.style.display = 'block';
                    document.getElementById('remainingTime').textContent = formatTime(data.remaining);
                } else {
                    remContainer.style.display = 'none';
                }
                
                // Settings sync
                document.getElementById('autoTempToggle').checked = data.autoTempMode;
                document.getElementById('tempThreshold').value = data.tempThreshold;
                document.getElementById('timerEnabledToggle').checked = data.timerEnabled;
                document.getElementById('timerInputs').style.display = data.timerEnabled ? 'block' : 'none';
            } catch (e) {
                console.error(e);
            }
        }
        
        function formatTime(seconds) {
            const h = Math.floor(seconds / 3600);
            const m = Math.floor((seconds % 3600) / 60);
            const s = seconds % 60;
            return `${h.toString().padStart(2,'0')}:${m.toString().padStart(2,'0')}:${s.toString().padStart(2,'0')}`;
        }
        
        async function toggleRelay() {
            const isOn = document.getElementById('relayToggle').checked;
            await fetch(isOn ? '/on' : '/off');
        }
        
        async function toggleAutoTemp() {
            const enabled = document.getElementById('autoTempToggle').checked;
            await fetch(`/autotemp?enable=${enabled ? 1 : 0}`);
        }
        
        async function setTempThreshold() {
            const val = parseFloat(document.getElementById('tempThreshold').value);
            if (!isNaN(val)) await fetch(`/threshold?value=${val}`);
        }
        
        async function toggleTimerMode() {
            const enabled = document.getElementById('timerEnabledToggle').checked;
            await fetch(`/timermode?enable=${enabled ? 1 : 0}`);
            document.getElementById('timerInputs').style.display = enabled ? 'block' : 'none';
        }
        
        async function startTimer() {
            const min = parseInt(document.getElementById('minutes').value);
            if (min > 0) await fetch(`/timer?min=${min}`);
        }
        
        async function cancelTimer() {
            await fetch('/off');
        }
        
        window.onload = () => {
            fetchData();
            setInterval(fetchData, 1000);
        };
    </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", page);
}

void handleOn() {
  relayON();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleOff() {
  relayOFF();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleTimer() {
  if (server.hasArg("min")) {
    int minutes = server.arg("min").toInt();
    if (minutes > 0 && timerEnabled) {
      relayON();
      relayOffTime = millis() + (unsigned long)minutes * 60000UL;
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleAutoTemp() {
  if (server.hasArg("enable")) {
    autoTempMode = server.arg("enable").toInt() == 1;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleThreshold() {
  if (server.hasArg("value")) {
    tempThreshold = server.arg("value").toFloat();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleTimerMode() {
  if (server.hasArg("enable")) {
    timerEnabled = server.arg("enable").toInt() == 1;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  relayOFF();
  dht.begin();
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/timer", handleTimer);
  server.on("/autotemp", handleAutoTemp);
  server.on("/threshold", handleThreshold);
  server.on("/timermode", handleTimerMode);
  
  server.begin();
  Serial.println("Web server started!");
}

void loop() {
  server.handleClient();
  
  unsigned long now = millis();
  
  // Auto temperature control
  if (autoTempMode && currentTemp >= tempThreshold) {
    if (!relayState) relayON();
  }
  
  // Auto turn off timer
  if (relayState && relayOffTime > 0 && now >= relayOffTime) {
    relayOFF();
  }
}