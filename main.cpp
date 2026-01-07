/*
 * ESP32 Hardware Security Research Demo - Single File Implementation
 * 
 * PURPOSE: Educational demonstration of insecure IR/RF signal design vulnerabilities
 * SCOPE: Laboratory use only on owned devices
 * 
 * SECURITY CONCEPTS DEMONSTRATED:
 * - Lack of authentication in legacy IR protocols
 * - Replay attack vulnerabilities in simple RF systems
 * - Why rolling codes and encryption prevent these attacks
 * 
 * ETHICAL CONSTRAINTS:
 * - No jamming or interference
 * - No brute-force attacks
 * - No rolling-code circumvention
 * - Educational research only
 * 
 * LICENSE: Educational use only
 */

#include <WiFi.h>
#include <WebServer.h>
#include <vector>
#include <memory>

// ============================================================================
// COMPILE-TIME CONFIGURATION FLAGS
// ============================================================================
#define ENABLE_IR_MODULE 1    // Set to 0 to disable IR functionality
#define ENABLE_RF_MODULE 1    // Set to 0 to disable RF functionality

// ============================================================================
// HARDWARE PIN DEFINITIONS
// ============================================================================
#define IR_RECV_PIN      15   // IR receiver data pin
#define IR_SEND_PIN      4    // IR LED transmit pin
#define RF_RECV_PIN      14   // 433MHz receiver data pin
#define RF_SEND_PIN      12   // 433MHz transmitter data pin
#define STATUS_LED_PIN   2    // Built-in LED for status indication

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================
#define MAX_SIGNAL_LENGTH 500         // Maximum raw timing values to capture
#define MAX_STORED_SIGNALS 20         // Maximum signals to store in memory
#define FAILSAFE_TIMEOUT_MS 30000     // 30 second max replay duration
#define MAX_LOG_ENTRIES 10            // Activity log size

// WiFi Access Point Configuration
const char* AP_SSID = "ESP32-SecurityLab";
const char* AP_PASSWORD = "research2024";

// ============================================================================
// ENUMERATIONS AND STRUCTURES
// ============================================================================

enum SignalType {
    SIGNAL_TYPE_IR,
    SIGNAL_TYPE_RF
};

enum SystemState {
    STATE_IDLE,
    STATE_CAPTURING,
    STATE_REPLAYING
};

struct RawSignal {
    SignalType type;
    unsigned long timestamp;
    uint16_t length;
    uint16_t timings[MAX_SIGNAL_LENGTH];
    char id[16];
};

struct ActivityLogEntry {
    unsigned long timestamp;
    char message[64];
};

// ============================================================================
// GLOBAL STATE
// ============================================================================

WebServer server(80);
std::vector<RawSignal> capturedSignals;
std::vector<ActivityLogEntry> activityLog;

SystemState currentState = STATE_IDLE;
unsigned long stateStartTime = 0;
unsigned long signalCounter = 0;

// Attack simulation parameters
bool attackSimulationActive = false;
int attackDelayMs = 1000;
int attackSignalIndex = 0;
unsigned long lastAttackTime = 0;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void setStatusLED(SystemState state) {
    switch(state) {
        case STATE_IDLE:
            digitalWrite(STATUS_LED_PIN, LOW);
            break;
        case STATE_CAPTURING:
            // Blink fast during capture
            digitalWrite(STATUS_LED_PIN, (millis() / 100) % 2);
            break;
        case STATE_REPLAYING:
            digitalWrite(STATUS_LED_PIN, HIGH);
            break;
    }
}

void addActivityLog(const char* message) {
    ActivityLogEntry entry;
    entry.timestamp = millis();
    strncpy(entry.message, message, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';
    
    activityLog.push_back(entry);
    
    // Keep only last MAX_LOG_ENTRIES
    if(activityLog.size() > MAX_LOG_ENTRIES) {
        activityLog.erase(activityLog.begin());
    }
    
    Serial.println(message);
}

// Generate unique signal ID
void generateSignalId(char* buffer, size_t size, SignalType type) {
    const char* prefix = (type == SIGNAL_TYPE_IR) ? "IR" : "RF";
    snprintf(buffer, size, "%s_%lu", prefix, signalCounter++);
}

// ============================================================================
// IR CAPTURE AND REPLAY FUNCTIONS
// ============================================================================

#if ENABLE_IR_MODULE

/*
 * IR Signal Capture
 * 
 * VULNERABILITY DEMONSTRATED:
 * Most consumer IR remotes use simple pulse-width modulation without
 * authentication. This allows trivial replay attacks.
 * 
 * WHY THIS WORKS ON INSECURE SYSTEMS:
 * - No rolling codes
 * - No encryption
 * - No timestamp validation
 * - Device accepts any valid timing pattern
 * 
 * SECURE ALTERNATIVES:
 * - Challenge-response authentication
 * - Time-based one-time codes
 * - Encrypted command payloads
 */

bool captureIRSignal(RawSignal& signal) {
    const unsigned long timeout = 150000; // 150ms timeout
    const unsigned int minPulse = 50;     // Minimum pulse width (microseconds)
    const unsigned int maxPulse = 15000;  // Maximum pulse width
    
    signal.type = SIGNAL_TYPE_IR;
    signal.length = 0;
    signal.timestamp = millis();
    generateSignalId(signal.id, sizeof(signal.id), SIGNAL_TYPE_IR);
    
    unsigned long startTime = micros();
    int currentState = digitalRead(IR_RECV_PIN);
    int lastState = currentState;
    unsigned long lastChange = startTime;
    
    // Wait for signal start (LOW)
    while(digitalRead(IR_RECV_PIN) == HIGH) {
        if(micros() - startTime > timeout) {
            return false; // No signal detected
        }
    }
    
    addActivityLog("IR capture started");
    
    // Capture timing data
    while(signal.length < MAX_SIGNAL_LENGTH) {
        currentState = digitalRead(IR_RECV_PIN);
        unsigned long now = micros();
        
        if(currentState != lastState) {
            unsigned long duration = now - lastChange;
            
            if(duration >= minPulse && duration <= maxPulse) {
                signal.timings[signal.length++] = (uint16_t)duration;
            }
            
            lastChange = now;
            lastState = currentState;
        }
        
        // End of signal detection (silence)
        if(now - lastChange > timeout) {
            break;
        }
    }
    
    if(signal.length > 10) { // Minimum valid signal length
        char logMsg[64];
        snprintf(logMsg, sizeof(logMsg), "IR signal captured: %s (%d timings)", 
                 signal.id, signal.length);
        addActivityLog(logMsg);
        return true;
    }
    
    return false;
}

void replayIRSignal(const RawSignal& signal) {
    if(signal.type != SIGNAL_TYPE_IR) return;
    
    addActivityLog("Replaying IR signal");
    
    // Replay the captured timing pattern
    for(uint16_t i = 0; i < signal.length; i++) {
        if(i % 2 == 0) {
            digitalWrite(IR_SEND_PIN, HIGH);
        } else {
            digitalWrite(IR_SEND_PIN, LOW);
        }
        delayMicroseconds(signal.timings[i]);
    }
    digitalWrite(IR_SEND_PIN, LOW);
    
    char logMsg[64];
    snprintf(logMsg, sizeof(logMsg), "IR signal replayed: %s", signal.id);
    addActivityLog(logMsg);
}

#endif // ENABLE_IR_MODULE

// ============================================================================
// RF CAPTURE AND REPLAY FUNCTIONS
// ============================================================================

#if ENABLE_RF_MODULE

/*
 * RF 433MHz Signal Capture
 * 
 * VULNERABILITY DEMONSTRATED:
 * Simple fixed-code RF systems transmit the same signal every time.
 * These are vulnerable to replay attacks.
 * 
 * WHY THIS WORKS ON INSECURE SYSTEMS:
 * - Static codes (same every press)
 * - No encryption
 * - No authentication handshake
 * - Receiver accepts any matching code
 * 
 * SYSTEMS THIS DOES NOT AFFECT:
 * - Rolling code systems (KeeLoq, etc.)
 * - Encrypted RF protocols
 * - Challenge-response systems
 * - Modern car key fobs with crypto
 * 
 * SECURE ALTERNATIVES:
 * - Rolling code algorithms
 * - AES encryption
 * - Frequency hopping
 * - Authentication protocols
 */

bool captureRFSignal(RawSignal& signal) {
    const unsigned long timeout = 500000; // 500ms timeout
    const unsigned int minPulse = 100;
    const unsigned int maxPulse = 20000;
    
    signal.type = SIGNAL_TYPE_RF;
    signal.length = 0;
    signal.timestamp = millis();
    generateSignalId(signal.id, sizeof(signal.id), SIGNAL_TYPE_RF);
    
    unsigned long startTime = micros();
    int currentState = digitalRead(RF_RECV_PIN);
    int lastState = currentState;
    unsigned long lastChange = startTime;
    
    // Wait for signal activity
    while(micros() - startTime < timeout) {
        currentState = digitalRead(RF_RECV_PIN);
        
        if(currentState != lastState) {
            unsigned long now = micros();
            unsigned long duration = now - lastChange;
            
            if(duration >= minPulse && duration <= maxPulse) {
                signal.timings[signal.length++] = (uint16_t)duration;
                
                if(signal.length >= MAX_SIGNAL_LENGTH) {
                    break;
                }
            }
            
            lastChange = now;
            lastState = currentState;
        }
        
        // Check for end of transmission
        if(signal.length > 0 && (micros() - lastChange) > 10000) {
            break;
        }
    }
    
    if(signal.length > 20) { // Minimum valid RF signal
        char logMsg[64];
        snprintf(logMsg, sizeof(logMsg), "RF signal captured: %s (%d timings)", 
                 signal.id, signal.length);
        addActivityLog(logMsg);
        return true;
    }
    
    return false;
}

void replayRFSignal(const RawSignal& signal) {
    if(signal.type != SIGNAL_TYPE_RF) return;
    
    addActivityLog("Replaying RF signal");
    
    // Replay the captured RF pattern
    for(uint16_t i = 0; i < signal.length; i++) {
        digitalWrite(RF_SEND_PIN, i % 2 == 0 ? HIGH : LOW);
        delayMicroseconds(signal.timings[i]);
    }
    digitalWrite(RF_SEND_PIN, LOW);
    
    char logMsg[64];
    snprintf(logMsg, sizeof(logMsg), "RF signal replayed: %s", signal.id);
    addActivityLog(logMsg);
}

#endif // ENABLE_RF_MODULE

// ============================================================================
// WEB SERVER HANDLERS
// ============================================================================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Security Research Lab</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
            background: #f5f5f5;
        }
        .header {
            background: #2c3e50;
            color: white;
            padding: 20px;
            border-radius: 8px;
            margin-bottom: 20px;
        }
        .warning {
            background: #e74c3c;
            color: white;
            padding: 15px;
            border-radius: 5px;
            margin-bottom: 20px;
        }
        .card {
            background: white;
            padding: 20px;
            border-radius: 8px;
            margin-bottom: 20px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .status {
            display: inline-block;
            padding: 5px 15px;
            border-radius: 20px;
            font-weight: bold;
            margin-left: 10px;
        }
        .status-idle { background: #95a5a6; color: white; }
        .status-capturing { background: #f39c12; color: white; }
        .status-replaying { background: #e74c3c; color: white; }
        button {
            background: #3498db;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 14px;
            margin: 5px;
        }
        button:hover { background: #2980b9; }
        button:disabled { background: #bdc3c7; cursor: not-allowed; }
        .danger { background: #e74c3c; }
        .danger:hover { background: #c0392b; }
        .success { background: #27ae60; }
        .success:hover { background: #229954; }
        table {
            width: 100%;
            border-collapse: collapse;
        }
        th, td {
            padding: 10px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th { background: #34495e; color: white; }
        .log-entry {
            padding: 5px;
            margin: 2px 0;
            background: #ecf0f1;
            border-left: 3px solid #3498db;
            font-family: monospace;
            font-size: 12px;
        }
        input[type="number"] {
            padding: 8px;
            border: 1px solid #ddd;
            border-radius: 4px;
            width: 100px;
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>🔒 ESP32 Hardware Security Research Lab</h1>
        <p>Educational Demonstration of Insecure Signal Design Vulnerabilities</p>
        <div>
            Status: <span class="status status-idle" id="systemStatus">IDLE</span>
            Signal Count: <strong id="signalCount">0</strong>
        </div>
    </div>

    <div class="warning">
        ⚠️ <strong>EDUCATIONAL USE ONLY</strong> - This system demonstrates why legacy IR/RF systems 
        without authentication are vulnerable to replay attacks. Modern secure systems use rolling codes 
        and encryption to prevent these attacks. Use only on devices you own in a controlled lab environment.
    </div>

    <div class="card">
        <h2>📡 Signal Capture</h2>
        <p>Capture IR or RF signals from insecure devices. This demonstrates why authentication is critical.</p>
        <button onclick="captureSignal('IR')" id="btnCaptureIR">Capture IR Signal</button>
        <button onclick="captureSignal('RF')" id="btnCaptureRF">Capture RF Signal</button>
    </div>

    <div class="card">
        <h2>📋 Captured Signals</h2>
        <table id="signalTable">
            <thead>
                <tr>
                    <th>ID</th>
                    <th>Type</th>
                    <th>Length</th>
                    <th>Timestamp</th>
                    <th>Action</th>
                </tr>
            </thead>
            <tbody id="signalTableBody">
                <tr><td colspan="5" style="text-align:center;">No signals captured</td></tr>
            </tbody>
        </table>
    </div>

    <div class="card">
        <h2>🎯 Attack Simulation Mode</h2>
        <p>Demonstrates sequential replay attacks on insecure systems. Includes failsafe timeout protection.</p>
        <div>
            <label>Delay between replays (ms): </label>
            <input type="number" id="attackDelay" value="1000" min="500" max="10000" step="100">
        </div>
        <div style="margin-top: 10px;">
            <button onclick="startAttackSim()" class="danger" id="btnStartAttack">Start Sequential Replay</button>
            <button onclick="stopAttackSim()" class="success" id="btnStopAttack">Stop Simulation</button>
        </div>
    </div>

    <div class="card">
        <h2>📊 Activity Log</h2>
        <div id="activityLog" style="max-height: 300px; overflow-y: auto;">
            <div class="log-entry">System initialized</div>
        </div>
    </div>

    <div class="card">
        <h2>ℹ️ Security Concepts</h2>
        <h3>Why Replay Attacks Work on Insecure Systems:</h3>
        <ul>
            <li><strong>No Authentication:</strong> Device accepts any valid timing pattern</li>
            <li><strong>Static Codes:</strong> Same signal transmitted every time</li>
            <li><strong>No Encryption:</strong> Signals transmitted in plain form</li>
        </ul>
        <h3>How Secure Systems Prevent This:</h3>
        <ul>
            <li><strong>Rolling Codes:</strong> Code changes with each transmission</li>
            <li><strong>Challenge-Response:</strong> Requires cryptographic handshake</li>
            <li><strong>Encryption:</strong> AES or similar protects command data</li>
            <li><strong>Timestamps:</strong> Prevents replay of old signals</li>
        </ul>
    </div>

    <script>
        function updateStatus() {
            fetch('/api/status')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('systemStatus').textContent = data.state;
                    document.getElementById('systemStatus').className = 'status status-' + data.state.toLowerCase();
                    document.getElementById('signalCount').textContent = data.signalCount;
                    
                    updateSignalTable(data.signals);
                    updateActivityLog(data.log);
                });
        }

        function updateSignalTable(signals) {
            const tbody = document.getElementById('signalTableBody');
            if(signals.length === 0) {
                tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;">No signals captured</td></tr>';
                return;
            }
            
            tbody.innerHTML = signals.map((s, idx) => 
                `<tr>
                    <td>${s.id}</td>
                    <td>${s.type}</td>
                    <td>${s.length}</td>
                    <td>${s.timestamp}</td>
                    <td><button onclick="replaySignal(${idx})">Replay</button></td>
                </tr>`
            ).join('');
        }

        function updateActivityLog(log) {
            const logDiv = document.getElementById('activityLog');
            logDiv.innerHTML = log.map(entry => 
                `<div class="log-entry">[${entry.timestamp}] ${entry.message}</div>`
            ).reverse().join('');
        }

        function captureSignal(type) {
            fetch('/api/capture?type=' + type)
                .then(r => r.json())
                .then(data => {
                    alert(data.message);
                    updateStatus();
                });
        }

        function replaySignal(index) {
            fetch('/api/replay?index=' + index)
                .then(r => r.json())
                .then(data => {
                    alert(data.message);
                    updateStatus();
                });
        }

        function startAttackSim() {
            const delay = document.getElementById('attackDelay').value;
            fetch('/api/attack/start?delay=' + delay)
                .then(r => r.json())
                .then(data => {
                    alert(data.message);
                    updateStatus();
                });
        }

        function stopAttackSim() {
            fetch('/api/attack/stop')
                .then(r => r.json())
                .then(data => {
                    alert(data.message);
                    updateStatus();
                });
        }

        // Update status every 2 seconds
        setInterval(updateStatus, 2000);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

void handleRoot() {
    server.send(200, "text/html", HTML_PAGE);
}

void handleStatus() {
    String json = "{";
    
    // System state
    const char* stateName[] = {"IDLE", "CAPTURING", "REPLAYING"};
    json += "\"state\":\"" + String(stateName[currentState]) + "\",";
    json += "\"signalCount\":" + String(signalCounter) + ",";
    
    // Signals array
    json += "\"signals\":[";
    for(size_t i = 0; i < capturedSignals.size(); i++) {
        if(i > 0) json += ",";
        json += "{";
        json += "\"id\":\"" + String(capturedSignals[i].id) + "\",";
        json += "\"type\":\"" + String(capturedSignals[i].type == SIGNAL_TYPE_IR ? "IR" : "RF") + "\",";
        json += "\"length\":" + String(capturedSignals[i].length) + ",";
        json += "\"timestamp\":" + String(capturedSignals[i].timestamp);
        json += "}";
    }
    json += "],";
    
    // Activity log
    json += "\"log\":[";
    for(size_t i = 0; i < activityLog.size(); i++) {
        if(i > 0) json += ",";
        json += "{";
        json += "\"timestamp\":" + String(activityLog[i].timestamp) + ",";
        json += "\"message\":\"" + String(activityLog[i].message) + "\"";
        json += "}";
    }
    json += "]";
    
    json += "}";
    
    server.send(200, "application/json", json);
}

void handleCapture() {
    if(currentState != STATE_IDLE) {
        server.send(400, "application/json", "{\"message\":\"System busy\"}");
        return;
    }
    
    String type = server.arg("type");
    RawSignal signal;
    bool success = false;
    
    currentState = STATE_CAPTURING;
    stateStartTime = millis();
    
    if(type == "IR") {
        #if ENABLE_IR_MODULE
        success = captureIRSignal(signal);
        #else
        server.send(400, "application/json", "{\"message\":\"IR module disabled\"}");
        currentState = STATE_IDLE;
        return;
        #endif
    } else if(type == "RF") {
        #if ENABLE_RF_MODULE
        success = captureRFSignal(signal);
        #else
        server.send(400, "application/json", "{\"message\":\"RF module disabled\"}");
        currentState = STATE_IDLE;
        return;
        #endif
    }
    
    currentState = STATE_IDLE;
    
    if(success) {
        if(capturedSignals.size() >= MAX_STORED_SIGNALS) {
            capturedSignals.erase(capturedSignals.begin());
        }
        capturedSignals.push_back(signal);
        
        String msg = "{\"message\":\"Signal captured: " + String(signal.id) + "\"}";
        server.send(200, "application/json", msg);
    } else {
        server.send(400, "application/json", "{\"message\":\"Capture failed or timeout\"}");
    }
}

void handleReplay() {
    if(currentState != STATE_IDLE) {
        server.send(400, "application/json", "{\"message\":\"System busy\"}");
        return;
    }
    
    int index = server.arg("index").toInt();
    
    if(index < 0 || index >= capturedSignals.size()) {
        server.send(400, "application/json", "{\"message\":\"Invalid signal index\"}");
        return;
    }
    
    currentState = STATE_REPLAYING;
    stateStartTime = millis();
    
    const RawSignal& signal = capturedSignals[index];
    
    if(signal.type == SIGNAL_TYPE_IR) {
        #if ENABLE_IR_MODULE
        replayIRSignal(signal);
        #endif
    } else {
        #if ENABLE_RF_MODULE
        replayRFSignal(signal);
        #endif
    }
    
    currentState = STATE_IDLE;
    
    server.send(200, "application/json", "{\"message\":\"Signal replayed\"}");
}

void handleAttackStart() {
    if(capturedSignals.empty()) {
        server.send(400, "application/json", "{\"message\":\"No signals to replay\"}");
        return;
    }
    
    attackDelayMs = server.arg("delay").toInt();
    if(attackDelayMs < 500) attackDelayMs = 500;
    if(attackDelayMs > 10000) attackDelayMs = 10000;
    
    attackSimulationActive = true;
    attackSignalIndex = 0;
    lastAttackTime = 0;
    stateStartTime = millis();
    
    addActivityLog("Attack simulation started");
    
    server.send(200, "application/json", "{\"message\":\"Attack simulation started\"}");
}

void handleAttackStop() {
    attackSimulationActive = false;
    currentState = STATE_IDLE;
    
    addActivityLog("Attack simulation stopped");
    
    server.send(200, "application/json", "{\"message\":\"Attack simulation stopped\"}");
}

// ============================================================================
// MAIN SETUP AND LOOP
// ============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n===========================================");
    Serial.println("ESP32 Hardware Security Research Lab");
    Serial.println("Educational Demonstration System");
    Serial.println("===========================================\n");
    
    // Initialize pins
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    #if ENABLE_IR_MODULE
    pinMode(IR_RECV_PIN, INPUT);
    pinMode(IR_SEND_PIN, OUTPUT);
    digitalWrite(IR_SEND_PIN, LOW);
    Serial.println("[✓] IR module enabled");
    #else
    Serial.println("[✗] IR module disabled");
    #endif
    
    #if ENABLE_RF_MODULE
    pinMode(RF_RECV_PIN, INPUT);
    pinMode(RF_SEND_PIN, OUTPUT);
    digitalWrite(RF_SEND_PIN, LOW);
    Serial.println("[✓] RF module enabled");
    #else
    Serial.println("[✗] RF module disabled");
    #endif
    
    // Start WiFi Access Point
    Serial.println("\nStarting WiFi Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    Serial.print("SSID: ");
    Serial.println(AP_SSID);
    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);
    
    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/api/status", handleStatus);
    server.on("/api/capture", handleCapture);
    server.on("/api/replay", handleReplay);
    server.on("/api/attack/start", handleAttackStart);
    server.on("/api/attack/stop", handleAttackStop);
    
    server.begin();
    Serial.println("\n[✓] Web server started");
    Serial.println("[✓] System ready");
    Serial.println("\nConnect to WiFi and navigate to: http://" + IP.toString());
    Serial.println("===========================================\n");
    
    addActivityLog("System initialized");
}

void loop() {
    server.handleClient();
    
    // Update LED status
    setStatusLED(currentState);
    
    // Failsafe timeout check
    if(currentState != STATE_IDLE) {
        if(millis() - stateStartTime > FAILSAFE_TIMEOUT_MS) {
            Serial.println("FAILSAFE: Operation timeout, returning to idle");
            currentState = STATE_IDLE;
            attackSimulationActive = false;
            addActivityLog("FAILSAFE: Timeout triggered");
        }
    }
    
    // Attack simulation logic
    if(attackSimulationActive && !capturedSignals.empty()) {
        unsigned long now = millis();
        
        if(now - lastAttackTime >= attackDelayMs) {
            if(currentState == STATE_IDLE) {
                currentState = STATE_REPLAYING;
                
                const RawSignal& signal = capturedSignals[attackSignalIndex];
                
                if(signal.type == SIGNAL_TYPE_IR) {
                    #if ENABLE_IR_MODULE
                    replayIRSignal(signal);
                    #endif
                } else {
                    #if ENABLE_RF_MODULE
                    replayRFSignal(signal);
                    #endif
                }
                
                // Move to next signal
                attackSignalIndex = (attackSignalIndex + 1) % capturedSignals.size();
                lastAttackTime = now;
                
                currentState = STATE_IDLE;
            }
        }
    }
    
    delay(10); // Small delay to prevent watchdog issues
}
