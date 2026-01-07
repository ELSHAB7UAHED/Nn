/*
 * ESP32 Hardware Security Research Demo
 * Educational Platform for Demonstrating IR/RF Signal Vulnerabilities
 * 
 * PURPOSE: Laboratory demonstration of insecure signal design patterns
 * SCOPE: Owned devices only, controlled environment, educational use
 * 
 * SECURITY CONCEPTS DEMONSTRATED:
 * 1. Replay attacks on unauthenticated signals
 * 2. Why static signals are vulnerable
 * 3. Importance of rolling codes and authentication
 * 
 * WHAT THIS DOES NOT AFFECT:
 * - Modern car key fobs (rolling codes)
 * - Encrypted RF systems
 * - Challenge-response authentication
 * - Secure IoT devices with proper authentication
 * 
 * License: Educational Use Only
 */

#include <WiFi.h>
#include <WebServer.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

// ============================================================================
// COMPILE-TIME CONFIGURATION
// ============================================================================
#define ENABLE_IR_MODULE true
#define ENABLE_RF_MODULE true
#define MAX_SIGNALS 20
#define MAX_LOG_ENTRIES 10
#define REPLAY_TIMEOUT_MS 30000  // Failsafe: max 30 seconds continuous replay

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
const int IR_RECV_PIN = 15;
const int IR_SEND_PIN = 4;
const int RF_RECV_PIN = 14;
const int RF_SEND_PIN = 12;
const int LED_STATUS_PIN = 2;

// ============================================================================
// WIFI ACCESS POINT CONFIGURATION
// ============================================================================
const char* AP_SSID = "ESP32-Security-Lab";
const char* AP_PASSWORD = "research2024";

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
WebServer server(80);
IRrecv* irrecv = nullptr;
IRsend* irsend = nullptr;

// ============================================================================
// SIGNAL STORAGE STRUCTURE
// ============================================================================
struct CapturedSignal {
  uint8_t id;
  char type[4];  // "IR" or "RF"
  unsigned long timestamp;
  uint16_t length;
  uint16_t* rawData;
  bool valid;
};

struct ActivityLog {
  unsigned long timestamp;
  char event[64];
};

// ============================================================================
// GLOBAL STATE
// ============================================================================
CapturedSignal signals[MAX_SIGNALS];
ActivityLog activityLog[MAX_LOG_ENTRIES];
uint8_t signalCount = 0;
uint8_t logIndex = 0;
uint32_t totalSignalsCaptured = 0;

enum SystemState { IDLE, CAPTURING, REPLAYING };
SystemState currentState = IDLE;

bool replayActive = false;
unsigned long replayStartTime = 0;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void setLEDStatus(SystemState state) {
  switch(state) {
    case IDLE:
      digitalWrite(LED_STATUS_PIN, LOW);
      break;
    case CAPTURING:
      // Slow blink
      digitalWrite(LED_STATUS_PIN, (millis() / 500) % 2);
      break;
    case REPLAYING:
      // Fast blink
      digitalWrite(LED_STATUS_PIN, (millis() / 200) % 2);
      break;
  }
}

void addLog(const char* event) {
  snprintf(activityLog[logIndex].event, 64, "%s", event);
  activityLog[logIndex].timestamp = millis();
  logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;
}

uint8_t findFreeSlot() {
  for(uint8_t i = 0; i < MAX_SIGNALS; i++) {
    if(!signals[i].valid) return i;
  }
  return 0; // Overwrite oldest if full
}

// ============================================================================
// IR SIGNAL HANDLING
// ============================================================================

void captureIRSignal() {
  #if ENABLE_IR_MODULE
  decode_results results;
  
  if(irrecv->decode(&results)) {
    uint8_t slot = findFreeSlot();
    
    // Free previous data if exists
    if(signals[slot].valid && signals[slot].rawData != nullptr) {
      delete[] signals[slot].rawData;
    }
    
    // Store new signal
    signals[slot].id = slot;
    strcpy(signals[slot].type, "IR");
    signals[slot].timestamp = millis();
    signals[slot].length = results.rawlen;
    signals[slot].rawData = new uint16_t[results.rawlen];
    
    for(uint16_t i = 0; i < results.rawlen; i++) {
      signals[slot].rawData[i] = results.rawbuf[i] * kRawTick;
    }
    
    signals[slot].valid = true;
    totalSignalsCaptured++;
    
    if(slot >= signalCount) signalCount = slot + 1;
    
    char logMsg[64];
    snprintf(logMsg, 64, "IR signal captured (ID: %d, Length: %d)", slot, results.rawlen);
    addLog(logMsg);
    
    irrecv->resume();
  }
  #endif
}

void replayIRSignal(uint8_t id) {
  #if ENABLE_IR_MODULE
  if(id >= MAX_SIGNALS || !signals[id].valid) return;
  if(strcmp(signals[id].type, "IR") != 0) return;
  
  currentState = REPLAYING;
  irsend->sendRaw(signals[id].rawData, signals[id].length, 38);
  
  char logMsg[64];
  snprintf(logMsg, 64, "IR signal replayed (ID: %d)", id);
  addLog(logMsg);
  
  delay(100); // Brief delay between replays
  currentState = IDLE;
  #endif
}

// ============================================================================
// RF SIGNAL HANDLING (433MHz)
// ============================================================================

volatile uint16_t rfTimings[512];
volatile uint16_t rfTimingIndex = 0;
volatile unsigned long lastRFChange = 0;

void IRAM_ATTR rfInterruptHandler() {
  #if ENABLE_RF_MODULE
  unsigned long now = micros();
  unsigned long duration = now - lastRFChange;
  
  if(duration > 10000) { // Reset on long gap (10ms)
    rfTimingIndex = 0;
  } else if(rfTimingIndex < 512) {
    rfTimings[rfTimingIndex++] = (uint16_t)duration;
  }
  
  lastRFChange = now;
  #endif
}

void captureRFSignal() {
  #if ENABLE_RF_MODULE
  if(rfTimingIndex > 20) { // Minimum signal length
    uint8_t slot = findFreeSlot();
    
    if(signals[slot].valid && signals[slot].rawData != nullptr) {
      delete[] signals[slot].rawData;
    }
    
    signals[slot].id = slot;
    strcpy(signals[slot].type, "RF");
    signals[slot].timestamp = millis();
    signals[slot].length = rfTimingIndex;
    signals[slot].rawData = new uint16_t[rfTimingIndex];
    
    noInterrupts();
    for(uint16_t i = 0; i < rfTimingIndex; i++) {
      signals[slot].rawData[i] = rfTimings[i];
    }
    rfTimingIndex = 0;
    interrupts();
    
    signals[slot].valid = true;
    totalSignalsCaptured++;
    
    if(slot >= signalCount) signalCount = slot + 1;
    
    char logMsg[64];
    snprintf(logMsg, 64, "RF signal captured (ID: %d, Length: %d)", slot, signals[slot].length);
    addLog(logMsg);
  }
  #endif
}

void replayRFSignal(uint8_t id) {
  #if ENABLE_RF_MODULE
  if(id >= MAX_SIGNALS || !signals[id].valid) return;
  if(strcmp(signals[id].type, "RF") != 0) return;
  
  currentState = REPLAYING;
  pinMode(RF_SEND_PIN, OUTPUT);
  
  for(uint16_t i = 0; i < signals[id].length; i++) {
    digitalWrite(RF_SEND_PIN, i % 2);
    delayMicroseconds(signals[id].rawData[i]);
  }
  digitalWrite(RF_SEND_PIN, LOW);
  
  char logMsg[64];
  snprintf(logMsg, 64, "RF signal replayed (ID: %d)", id);
  addLog(logMsg);
  
  delay(100);
  currentState = IDLE;
  #endif
}

// ============================================================================
// WEB SERVER HANDLERS
// ============================================================================

const char HTML_HEADER[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Security Research Lab</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;padding:20px}
.container{max-width:1200px;margin:0 auto}
h1{color:#16db93;margin-bottom:10px}
.warning{background:#ff6b6b;color:#fff;padding:15px;border-radius:5px;margin:20px 0}
.card{background:#16213e;padding:20px;margin:20px 0;border-radius:8px;box-shadow:0 4px 6px rgba(0,0,0,0.3)}
.stats{display:flex;justify-content:space-around;margin:20px 0}
.stat-box{text-align:center;padding:15px;background:#0f3460;border-radius:5px;flex:1;margin:0 10px}
.stat-box h3{color:#16db93;font-size:2em}
.stat-box p{color:#aaa;margin-top:5px}
button{background:#16db93;color:#000;border:none;padding:12px 24px;border-radius:5px;cursor:pointer;font-size:1em;margin:5px}
button:hover{background:#13c482}
button.danger{background:#ff6b6b}
button.danger:hover{background:#e85555}
table{width:100%;border-collapse:collapse;margin-top:15px}
th{background:#0f3460;padding:12px;text-align:left;color:#16db93}
td{padding:10px;border-bottom:1px solid #0f3460}
.status{display:inline-block;padding:5px 10px;border-radius:3px;font-size:0.9em}
.status.idle{background:#555;color:#fff}
.status.capturing{background:#ffa500;color:#000}
.status.replaying{background:#ff6b6b;color:#fff}
.log-entry{padding:8px;margin:5px 0;background:#0f3460;border-left:3px solid #16db93;border-radius:3px}
.log-time{color:#888;font-size:0.85em}
</style>
</head>
<body>
<div class="container">
<h1>🔬 ESP32 Hardware Security Research Lab</h1>
<p style="color:#aaa;margin-bottom:20px">Educational Demonstration Platform - Controlled Environment Only</p>
)rawliteral";

const char HTML_FOOTER[] PROGMEM = R"rawliteral(
</div>
<script>
function refresh(){location.reload()}
function replaySignal(id){fetch('/replay?id='+id).then(()=>setTimeout(refresh,500))}
function deleteSignal(id){fetch('/delete?id='+id).then(refresh)}
function clearAll(){if(confirm('Clear all signals?'))fetch('/clear').then(refresh)}
function startAttack(){fetch('/attack/start').then(refresh)}
function stopAttack(){fetch('/attack/stop').then(refresh)}
setInterval(refresh,5000);
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  String html = FPSTR(HTML_HEADER);
  
  // Warning banner
  html += F("<div class='warning'>");
  html += F("⚠️ <strong>EDUCATIONAL USE ONLY</strong> - This system demonstrates replay vulnerabilities in INSECURE signal designs. ");
  html += F("Modern secure systems (rolling codes, authentication, encryption) are NOT vulnerable to these attacks.");
  html += F("</div>");
  
  // System status
  html += F("<div class='card'>");
  html += F("<h2>System Status</h2>");
  html += F("<p>State: <span class='status ");
  
  switch(currentState) {
    case IDLE: html += F("idle'>IDLE"); break;
    case CAPTURING: html += F("capturing'>CAPTURING"); break;
    case REPLAYING: html += F("replaying'>REPLAYING"); break;
  }
  html += F("</span></p>");
  html += F("</div>");
  
  // Statistics
  html += F("<div class='stats'>");
  html += F("<div class='stat-box'><h3>");
  html += String(totalSignalsCaptured);
  html += F("</h3><p>Total Captured</p></div>");
  html += F("<div class='stat-box'><h3>");
  html += String(signalCount);
  html += F("</h3><p>Stored Signals</p></div>");
  html += F("<div class='stat-box'><h3>");
  html += String(millis() / 1000);
  html += F("</h3><p>Uptime (sec)</p></div>");
  html += F("</div>");
  
  // Captured signals table
  html += F("<div class='card'>");
  html += F("<h2>Captured Signals</h2>");
  html += F("<button onclick='clearAll()' class='danger'>Clear All</button>");
  html += F("<table><tr><th>ID</th><th>Type</th><th>Length</th><th>Age (sec)</th><th>Actions</th></tr>");
  
  for(uint8_t i = 0; i < signalCount; i++) {
    if(signals[i].valid) {
      html += F("<tr><td>") + String(signals[i].id) + F("</td>");
      html += F("<td>") + String(signals[i].type) + F("</td>");
      html += F("<td>") + String(signals[i].length) + F("</td>");
      html += F("<td>") + String((millis() - signals[i].timestamp) / 1000) + F("</td>");
      html += F("<td><button onclick='replaySignal(") + String(i) + F(")'>Replay</button>");
      html += F("<button onclick='deleteSignal(") + String(i) + F(")' class='danger'>Delete</button></td></tr>");
    }
  }
  
  html += F("</table></div>");
  
  // Attack simulation
  html += F("<div class='card'>");
  html += F("<h2>Attack Simulation Mode</h2>");
  html += F("<p style='color:#ffa500;margin-bottom:10px'>Sequential replay with configurable delay (demonstrates why static signals are insecure)</p>");
  html += F("<button onclick='startAttack()'>Start Sequential Replay</button>");
  html += F("<button onclick='stopAttack()' class='danger'>Stop Replay</button>");
  html += F("</div>");
  
  // Activity log
  html += F("<div class='card'>");
  html += F("<h2>Activity Log (Last 10 Events)</h2>");
  
  for(int i = MAX_LOG_ENTRIES - 1; i >= 0; i--) {
    int idx = (logIndex + i) % MAX_LOG_ENTRIES;
    if(activityLog[idx].timestamp > 0) {
      html += F("<div class='log-entry'>");
      html += F("<span class='log-time'>") + String(activityLog[idx].timestamp / 1000) + F("s</span> - ");
      html += String(activityLog[idx].event);
      html += F("</div>");
    }
  }
  
  html += F("</div>");
  
  // Educational notes
  html += F("<div class='card'>");
  html += F("<h2>📚 Why This Works (And Why It Shouldn't)</h2>");
  html += F("<p><strong>Vulnerable Systems:</strong> Basic IR remotes, simple 433MHz RF devices without authentication</p>");
  html += F("<p><strong>Secure Systems:</strong> Modern car keys (rolling codes), encrypted protocols, challenge-response auth</p>");
  html += F("<p><strong>Key Lesson:</strong> Static, unauthenticated signals can be captured and replayed. ");
  html += F("This is why security-critical systems MUST use encryption, rolling codes, or authentication.</p>");
  html += F("</div>");
  
  html += FPSTR(HTML_FOOTER);
  
  server.send(200, "text/html", html);
}

void handleReplay() {
  if(server.hasArg("id")) {
    uint8_t id = server.arg("id").toInt();
    
    if(strcmp(signals[id].type, "IR") == 0) {
      replayIRSignal(id);
    } else if(strcmp(signals[id].type, "RF") == 0) {
      replayRFSignal(id);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleDelete() {
  if(server.hasArg("id")) {
    uint8_t id = server.arg("id").toInt();
    if(id < MAX_SIGNALS && signals[id].valid) {
      if(signals[id].rawData != nullptr) {
        delete[] signals[id].rawData;
      }
      signals[id].valid = false;
      addLog("Signal deleted");
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleClear() {
  for(uint8_t i = 0; i < MAX_SIGNALS; i++) {
    if(signals[i].valid && signals[i].rawData != nullptr) {
      delete[] signals[i].rawData;
    }
    signals[i].valid = false;
  }
  signalCount = 0;
  addLog("All signals cleared");
  server.send(200, "text/plain", "OK");
}

void handleAttackStart() {
  replayActive = true;
  replayStartTime = millis();
  addLog("Sequential replay started");
  server.send(200, "text/plain", "Attack started");
}

void handleAttackStop() {
  replayActive = false;
  addLog("Sequential replay stopped");
  server.send(200, "text/plain", "Attack stopped");
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n===========================================");
  Serial.println("ESP32 Hardware Security Research Lab");
  Serial.println("Educational Platform - Controlled Use Only");
  Serial.println("===========================================\n");
  
  // Initialize LED
  pinMode(LED_STATUS_PIN, OUTPUT);
  digitalWrite(LED_STATUS_PIN, LOW);
  
  // Initialize signal storage
  for(uint8_t i = 0; i < MAX_SIGNALS; i++) {
    signals[i].valid = false;
    signals[i].rawData = nullptr;
  }
  
  // Initialize activity log
  for(uint8_t i = 0; i < MAX_LOG_ENTRIES; i++) {
    activityLog[i].timestamp = 0;
  }
  
  #if ENABLE_IR_MODULE
  // Initialize IR
  irrecv = new IRrecv(IR_RECV_PIN);
  irsend = new IRsend(IR_SEND_PIN);
  irrecv->enableIRIn();
  Serial.println("✓ IR module initialized");
  #endif
  
  #if ENABLE_RF_MODULE
  // Initialize RF
  pinMode(RF_RECV_PIN, INPUT);
  pinMode(RF_SEND_PIN, OUTPUT);
  digitalWrite(RF_SEND_PIN, LOW);
  attachInterrupt(digitalPinToInterrupt(RF_RECV_PIN), rfInterruptHandler, CHANGE);
  Serial.println("✓ RF module initialized");
  #endif
  
  // Setup WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  Serial.println("\n✓ WiFi Access Point started");
  Serial.print("  SSID: ");
  Serial.println(AP_SSID);
  Serial.print("  IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("  Password: ");
  Serial.println(AP_PASSWORD);
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/replay", handleReplay);
  server.on("/delete", handleDelete);
  server.on("/clear", handleClear);
  server.on("/attack/start", handleAttackStart);
  server.on("/attack/stop", handleAttackStop);
  
  server.begin();
  Serial.println("✓ Web server started\n");
  
  addLog("System initialized");
  Serial.println("System ready. Access web interface at: http://" + WiFi.softAPIP().toString());
  Serial.println("===========================================\n");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  // Handle web requests (non-blocking)
  server.handleClient();
  
  // Update LED status
  setLEDStatus(currentState);
  
  // Capture signals if in idle state
  if(currentState == IDLE) {
    #if ENABLE_IR_MODULE
    captureIRSignal();
    #endif
    
    #if ENABLE_RF_MODULE
    static unsigned long lastRFCheck = 0;
    if(millis() - lastRFCheck > 100) {
      captureRFSignal();
      lastRFCheck = millis();
    }
    #endif
  }
  
  // Handle attack simulation mode
  if(replayActive) {
    // Failsafe timeout
    if(millis() - replayStartTime > REPLAY_TIMEOUT_MS) {
      replayActive = false;
      addLog("Replay timeout - safety stop");
      Serial.println("⚠ Replay stopped: timeout reached (safety limit)");
    } else {
      // Sequentially replay all valid signals
      for(uint8_t i = 0; i < signalCount; i++) {
        if(signals[i].valid) {
          if(strcmp(signals[i].type, "IR") == 0) {
            replayIRSignal(i);
          } else if(strcmp(signals[i].type, "RF") == 0) {
            replayRFSignal(i);
          }
          delay(500); // Adjustable delay between signals
        }
      }
    }
  }
  
  // Small yield to prevent watchdog issues
  yield();
}
