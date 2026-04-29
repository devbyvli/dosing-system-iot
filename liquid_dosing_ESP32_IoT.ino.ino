/*
 * ============================================================
 *  AUTOMATIC LIQUID DOSING SYSTEM — IoT Edition
 *  Author  : Valentin Caloianu
 *  Platform: ESP32
 *  Sim     : Wokwi.com
 * ============================================================
 *  HARDWARE:
 *    - Ultrasonic sensor HC-SR04  (trigger: GPIO 5, echo: GPIO 18)
 *    - Relay module               (IN: GPIO 19) → controls pump
 *    - LCD 16x2 I2C               (SDA: GPIO 21, SCL: GPIO 22)
 *    - LED green  (pump ON)       (GPIO 2)
 *    - LED red    (tank FULL)     (GPIO 4)
 *    - Buzzer     (alert)         (GPIO 15)
 *
 *  IoT FEATURES:
 *    - WiFi connection (WPA2)
 *    - MQTT publish: sensor data every 2s
 *    - MQTT subscribe: remote pump control
 *    - Built-in web dashboard (HTTP on port 80)
 *    - OTA ready (base structure)
 *
 *  MQTT TOPICS:
 *    Publish:
 *      dosing/status     → JSON: {distance, pump, state, uptime}
 *      dosing/alert      → JSON: {type, message}
 *    Subscribe:
 *      dosing/control    → "ON" / "OFF" / "RESET"
 * ============================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ── WiFi credentials ────────────────────────────────────────
const char* WIFI_SSID     = "WokwiGuest";   // Wokwi built-in WiFi
const char* WIFI_PASSWORD = "";             // no password in Wokwi

// ── MQTT broker (public test broker) ────────────────────────
const char* MQTT_SERVER   = "broker.hivemq.com";
const int   MQTT_PORT     = 1883;
const char* MQTT_CLIENT   = "DosingSystem_VC_001";
const char* TOPIC_STATUS  = "dosing/status";
const char* TOPIC_ALERT   = "dosing/alert";
const char* TOPIC_CONTROL = "dosing/control";

// ── Pin definitions ─────────────────────────────────────────
#define TRIG_PIN       5
#define ECHO_PIN       18
#define RELAY_PIN      19
#define LED_GREEN      2
#define LED_RED        4
#define BUZZER_PIN     15

// ── Tank thresholds (cm from sensor to water surface) ───────
#define LEVEL_FULL     5
#define LEVEL_LOW      20
#define MAX_PUMP_TIME  10000UL  // 10s safety timeout

// ── Intervals ───────────────────────────────────────────────
#define MEASURE_INTERVAL  500UL
#define MQTT_INTERVAL     2000UL

// ── Objects ─────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient        wifiClient;
PubSubClient      mqtt(wifiClient);
WebServer         server(80);

// ── State machine ───────────────────────────────────────────
enum State { IDLE, PUMPING, FULL, ERROR_TIMEOUT };
State currentState = IDLE;

// ── Globals ─────────────────────────────────────────────────
unsigned long pumpStartTime  = 0;
unsigned long lastMeasure    = 0;
unsigned long lastMqttPublish= 0;
unsigned long errorResetTime = 0;
float         lastDistance   = 0;
bool          manualOverride = false;
String        stateNames[]   = {"IDLE","PUMPING","FULL","ERROR"};

// ════════════════════════════════════════════════════════════
//  HARDWARE HELPERS
// ════════════════════════════════════════════════════════════
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000);
  return (dur == 0) ? -1 : (dur * 0.0343f) / 2.0f;
}

void setPump(bool on) {
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  digitalWrite(LED_GREEN, on ? HIGH : LOW);
}

void alertBuzzer(int beeps) {
  for (int i = 0; i < beeps; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(100);
    digitalWrite(BUZZER_PIN, LOW);  delay(100);
  }
}

void updateLCD(float dist, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (dist < 0) lcd.print("Sensor ERROR    ");
  else {
    char buf[17];
    snprintf(buf, sizeof(buf), "Level:%4.1f cm W%c",
             dist, WiFi.isConnected() ? '*' : ' ');
    lcd.print(buf);
  }
  lcd.setCursor(0, 1);
  char padded[17];
  snprintf(padded, sizeof(padded), "%-16s", line2);
  lcd.print(padded);
}

// ════════════════════════════════════════════════════════════
//  WIFI
// ════════════════════════════════════════════════════════════
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("WiFi connecting ");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); Serial.print("."); attempts++;
  }
  if (WiFi.isConnected()) {
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    lcd.setCursor(0,1);
    lcd.print(WiFi.localIP().toString());
    delay(1500);
  } else {
    Serial.println("\nWiFi FAILED — running offline");
    lcd.setCursor(0,1); lcd.print("Offline mode    ");
    delay(1000);
  }
}

// ════════════════════════════════════════════════════════════
//  MQTT
// ════════════════════════════════════════════════════════════
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  Serial.println("MQTT recv [" + String(topic) + "]: " + msg);

  if (msg == "ON") {
    manualOverride = true;
    setPump(true);
    pumpStartTime = millis();
    currentState = PUMPING;
    Serial.println(">> Remote: Pump ON");
  } else if (msg == "OFF") {
    manualOverride = false;
    setPump(false);
    currentState = IDLE;
    Serial.println(">> Remote: Pump OFF");
  } else if (msg == "RESET") {
    manualOverride = false;
    setPump(false);
    currentState = IDLE;
    digitalWrite(LED_RED, LOW);
    Serial.println(">> Remote: RESET");
  }
}

void connectMQTT() {
  if (!WiFi.isConnected()) return;
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  Serial.print("Connecting to MQTT");
  int attempts = 0;
  while (!mqtt.connected() && attempts < 5) {
    Serial.print(".");
    if (mqtt.connect(MQTT_CLIENT)) {
      Serial.println(" OK");
      mqtt.subscribe(TOPIC_CONTROL);
      publishAlert("ONLINE", "Dosing system started");
    } else {
      delay(1000); attempts++;
    }
  }
  if (!mqtt.connected()) Serial.println(" MQTT FAILED");
}

void publishStatus(float dist) {
  if (!mqtt.connected()) return;
  StaticJsonDocument<200> doc;
  doc["distance"]  = (dist < 0) ? -1 : (int)dist;
  doc["pump"]      = (currentState == PUMPING);
  doc["state"]     = stateNames[(int)currentState];
  doc["uptime_s"]  = millis() / 1000;
  doc["ip"]        = WiFi.localIP().toString();
  char buf[200];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_STATUS, buf);
}

void publishAlert(const char* type, const char* message) {
  if (!mqtt.connected()) return;
  StaticJsonDocument<128> doc;
  doc["type"]    = type;
  doc["message"] = message;
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_ALERT, buf);
}

// ════════════════════════════════════════════════════════════
//  WEB DASHBOARD (served from ESP32)
// ════════════════════════════════════════════════════════════
void handleRoot() {
  String html = R"rawhtml(
<!DOCTYPE html><html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<meta http-equiv='refresh' content='2'>
<title>Dosing System</title>
<style>
  body{font-family:sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:20px}
  h1{color:#58a6ff;font-size:22px}
  .card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px;margin:10px 0}
  .val{font-size:28px;font-weight:700;color:#3fb950}
  .val.warn{color:#f85149}
  .val.pump{color:#d29922}
  label{font-size:12px;color:#8b949e;display:block;margin-bottom:4px}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
  button{background:#238636;color:#fff;border:none;border-radius:6px;
         padding:10px 20px;cursor:pointer;font-size:14px;margin:4px}
  button.off{background:#da3633}
  .state{display:inline-block;padding:4px 12px;border-radius:20px;font-size:13px;font-weight:600}
  .IDLE{background:#1f3d2b;color:#3fb950}
  .PUMPING{background:#3d2b1f;color:#d29922}
  .FULL{background:#1f2d3d;color:#58a6ff}
  .ERROR{background:#3d1f1f;color:#f85149}
</style>
</head>
<body>
<h1>💧 Dosing System Dashboard</h1>
<div class='card'>
  <div class='grid'>
    <div><label>Water Level</label>
      <div class='val )rawhtml";

  html += (lastDistance > LEVEL_LOW) ? "warn" : "";
  html += "'>";
  html += (lastDistance < 0) ? "ERR" : String((int)lastDistance) + " cm";
  html += R"rawhtml(</div></div>
    <div><label>State</label>
      <div class='state )rawhtml";
  html += stateNames[(int)currentState];
  html += "'>";
  html += stateNames[(int)currentState];
  html += R"rawhtml(</div></div>
    <div><label>Pump</label>
      <div class='val pump'>)rawhtml";
  html += (currentState == PUMPING) ? "ON 🟢" : "OFF ⚫";
  html += R"rawhtml(</div></div>
    <div><label>Uptime</label>
      <div class='val'>)rawhtml";
  html += String(millis() / 1000) + "s";
  html += R"rawhtml(</div></div>
  </div>
</div>
<div class='card'>
  <label>Remote Control</label><br>
  <form action='/control'>
    <button name='cmd' value='ON'>▶ Pump ON</button>
    <button class='off' name='cmd' value='OFF'>⏹ Pump OFF</button>
    <button name='cmd' value='RESET'>🔄 Reset</button>
  </form>
</div>
<div class='card'>
  <label>MQTT Topics</label>
  <p style='font-size:13px;color:#8b949e'>
    📤 Publish: <code>dosing/status</code> · <code>dosing/alert</code><br>
    📥 Subscribe: <code>dosing/control</code> (ON / OFF / RESET)<br>
    🌐 Broker: broker.hivemq.com:1883
  </p>
</div>
<p style='font-size:11px;color:#484f58'>Auto-refresh every 2s · Valentin Caloianu</p>
</body></html>
)rawhtml";

  server.send(200, "text/html", html);
}

void handleControl() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    // Reuse MQTT callback logic
    byte payload[10];
    cmd.getBytes(payload, cmd.length()+1);
    mqttCallback((char*)TOPIC_CONTROL, payload, cmd.length());
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["distance"] = (int)lastDistance;
  doc["pump"]     = (currentState == PUMPING);
  doc["state"]    = stateNames[(int)currentState];
  doc["uptime_s"] = millis() / 1000;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN,   OUTPUT);
  pinMode(ECHO_PIN,   INPUT);
  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  setPump(false);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0); lcd.print(" IoT Dosing Sys ");
  lcd.setCursor(0,1); lcd.print("  by V.Caloianu ");
  alertBuzzer(1);
  delay(1500);

  connectWiFi();
  connectMQTT();

  server.on("/",        handleRoot);
  server.on("/control", handleControl);
  server.on("/status",  handleStatus);
  server.begin();

  Serial.println("=== IoT Dosing System READY ===");
  if (WiFi.isConnected())
    Serial.println("Dashboard: http://" + WiFi.localIP().toString());
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  // Keep connections alive
  if (WiFi.isConnected() && !mqtt.connected()) connectMQTT();
  if (mqtt.connected()) mqtt.loop();
  server.handleClient();

  unsigned long now = millis();

  // ── Measure sensor ───────────────────────────────────────
  if (now - lastMeasure >= MEASURE_INTERVAL) {
    lastMeasure  = now;
    lastDistance = measureDistance();

    Serial.printf("[%lus] dist=%.1f cm | state=%s | pump=%s\n",
      now/1000, lastDistance,
      stateNames[(int)currentState].c_str(),
      (currentState == PUMPING) ? "ON" : "OFF");

    // ── State machine ─────────────────────────────────────
    switch (currentState) {

      case IDLE:
        setPump(false);
        digitalWrite(LED_RED, LOW);
        if (lastDistance < 0) {
          updateLCD(lastDistance, "Sensor Error!   ");
        } else if (lastDistance > LEVEL_LOW) {
          currentState  = PUMPING;
          pumpStartTime = now;
          setPump(true);
          updateLCD(lastDistance, "Pump ON  [LOW]  ");
          publishAlert("PUMP_START", "Tank low, pump started");
          Serial.println(">> PUMPING");
        } else {
          updateLCD(lastDistance, "OK - Monitoring ");
        }
        break;

      case PUMPING:
        if (!manualOverride && now - pumpStartTime >= MAX_PUMP_TIME) {
          setPump(false);
          currentState   = ERROR_TIMEOUT;
          errorResetTime = now;
          alertBuzzer(3);
          publishAlert("TIMEOUT", "Pump timeout - check tank");
          Serial.println(">> ERROR_TIMEOUT");
          break;
        }
        if (lastDistance >= 0 && lastDistance < LEVEL_FULL) {
          setPump(false);
          currentState = FULL;
          manualOverride = false;
          digitalWrite(LED_RED, HIGH);
          alertBuzzer(2);
          updateLCD(lastDistance, "FULL! Pump OFF  ");
          publishAlert("TANK_FULL", "Tank full, pump stopped");
          Serial.println(">> FULL");
        } else {
          int elapsed = (now - pumpStartTime) / 1000;
          char s[17];
          snprintf(s, sizeof(s), "Pumping...%3ds  ", elapsed);
          updateLCD(lastDistance, s);
        }
        break;

      case FULL:
        digitalWrite(LED_RED, HIGH);
        updateLCD(lastDistance, "Tank FULL       ");
        if (lastDistance > LEVEL_LOW) {
          currentState = IDLE;
          digitalWrite(LED_RED, LOW);
          Serial.println(">> IDLE");
        }
        break;

      case ERROR_TIMEOUT:
        setPump(false);
        updateLCD(lastDistance, "ERR: Timeout!   ");
        if (now - errorResetTime > 5000) {
          currentState = IDLE;
          Serial.println(">> IDLE (retry)");
        }
        break;
    }
  }

  // ── MQTT publish status ──────────────────────────────────
  if (now - lastMqttPublish >= MQTT_INTERVAL) {
    lastMqttPublish = now;
    publishStatus(lastDistance);
  }
}
