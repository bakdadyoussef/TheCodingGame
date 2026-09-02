#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT11.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>

#define DHTPIN 4
#define VIBRATION_PIN 6

const char* ssid = "fh_hp";
const char* password = "kingkingkingking";

DHT11 dht11(DHTPIN);
WebServer server(80);

// Store last valid readings
int lastTemperature = 0;
int lastHumidity = 0;

String getHTML() {
    int temperature = 0;
    int humidity = 0;
    int result = dht11.readTemperatureHumidity(temperature, humidity);
    
    if (result == 0) {
        lastTemperature = temperature;
        lastHumidity = humidity;
    }
    
    String vibrationState = (digitalRead(VIBRATION_PIN) == HIGH) ? 
                            "VIBRATION DETECTED" : "No Vibration";
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Dashboard</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: #f2f2f2;
            text-align: center;
            margin-top: 40px;
        }
        .card {
            width: 340px;
            margin: auto;
            padding: 20px;
            background: white;
            border-radius: 15px;
            box-shadow: 0 0 12px rgba(0,0,0,0.25);
        }
        .value {
            font-size: 24px;
            margin: 18px 0;
        }
        .good { color: green; }
        .bad { color: red; font-weight: bold; }
    </style>
    <script>
        setTimeout(function(){ location.reload(); }, 2000);
    </script>
</head>
<body>
    <div class="card">
        <h2>ESP32 Sensor Dashboard</h2>
)rawliteral";

    html += "<div class='value good'>Temperature : " + String(lastTemperature) + " °C</div>";
    html += "<div class='value good'>Humidity : " + String(lastHumidity) + " %</div>";
    
    if (result != 0) {
        html += "<div class='value bad'>⚠ Using Last Valid Reading</div>";
    }
    
    if (vibrationState == "VIBRATION DETECTED") {
        html += "<div class='value bad'>Vibration : " + vibrationState + "</div>";
    } else {
        html += "<div class='value good'>Vibration : " + vibrationState + "</div>";
    }
    
    html += "<div class='value'>Uptime : " + String(millis()/1000) + " s</div>";
    
    html += R"rawliteral(
    </div>
</body>
</html>
)rawliteral";
    
    return html;
}

// ==================== JSON API ====================
void handleData() {
    int temperature = 0;
    int humidity = 0;
    int result = dht11.readTemperatureHumidity(temperature, humidity);
    
    if (result == 0) {
        lastTemperature = temperature;
        lastHumidity = humidity;
    }
    
    bool vibrationDetected = digitalRead(VIBRATION_PIN) == HIGH;
    String vibrationState = vibrationDetected ? "VIBRATION DETECTED" : "No Vibration";
    
    JsonDocument doc;
    doc["status"] = (result == 0) ? "ok" : "using_last_valid";
    doc["temperature"] = lastTemperature;
    doc["humidity"] = lastHumidity;
    doc["vibrationDetected"] = vibrationDetected;
    doc["vibration"] = vibrationState;
    doc["lastReadSuccess"] = (result == 0);
    doc["uptime"] = millis() / 1000;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleNotFound() {
    server.send(404, "text/plain", "404 - Page Not Found");
}

void handleRoot() {
    server.send(200, "text/html; charset=UTF-8", getHTML());
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    pinMode(VIBRATION_PIN, INPUT);
    
    Serial.println("\nConnecting to WiFi...");
    WiFi.begin(ssid, password);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed!");
    }
    
    // mDNS
    if (MDNS.begin("esp32")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS started → http://esp32.local");
    } else {
        Serial.println("mDNS failed!");
    }
    
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.onNotFound(handleNotFound);
    
    server.begin();
    Serial.println("Web Server Started");
    Serial.println("→ HTML: http://esp32.local");
    Serial.println("→ JSON: http://esp32.local/data");
}

void loop() {
    server.handleClient();
}