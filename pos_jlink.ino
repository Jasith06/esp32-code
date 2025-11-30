#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <Preferences.h>

// ==================== HARDWARE CONFIGURATION ====================
#define QR_SCANNER_RX 16
#define QR_SCANNER_TX 17
#define LCD_I2C_ADDRESS 0x27
#define LCD_COLS 16
#define LCD_ROWS 2
#define STATUS_LED 2
#define BUZZER_PIN 4
#define CONFIG_BUTTON 0  // Boot button for manual config mode

// ==================== VERCEL DEPLOYMENT CONFIGURATION ====================
// 🌐 YOUR VERCEL DEPLOYMENT URL - CHANGE THIS!
// Example: "https://jlink-pos-web.vercel.app" or "https://your-project.vercel.app"
const String VERCEL_URL = "https://jlink-pos-web.vercel.app";  // ⚠️ CHANGE THIS TO YOUR VERCEL URL!

const String scannerAPI = VERCEL_URL + "/api/scanner";
const String testAPI = VERCEL_URL + "/api/test";

// ==================== GLOBAL OBJECTS ====================
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLS, LCD_ROWS);
WiFiManager wm;
Preferences preferences;

// ==================== VARIABLES ====================
unsigned long lastWifiCheck = 0;
unsigned long lastQRScan = 0;
unsigned long configButtonPressTime = 0;
bool wifiConnected = false;
bool configMode = false;
int scanCount = 0;
String currentProductCode = "";
String lastConnectedSSID = "";

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, QR_SCANNER_RX, QR_SCANNER_TX);
  
  initializePins();
  initializeLCD();
  loadPreferences();
  showStartupMessage();
  
  // Check if config button is pressed during startup
  if (digitalRead(CONFIG_BUTTON) == LOW) {
    Serial.println("⚙️ Config button pressed - Entering configuration mode");
    delay(1000); // Debounce
    enterConfigMode();
  } else {
    connectToWiFi();
  }
  
  if (wifiConnected) {
    testVercelConnection();
  }
  
  Serial.println("🚀 JLINK POS Scanner System Ready!");
  Serial.println("📡 Vercel Server: " + VERCEL_URL);
  beep(1);
}

void initializePins() {
  pinMode(STATUS_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(CONFIG_BUTTON, INPUT_PULLUP);
  digitalWrite(STATUS_LED, LOW);
}

void loadPreferences() {
  preferences.begin("jlink-pos", false);
  scanCount = preferences.getInt("scanCount", 0);
  lastConnectedSSID = preferences.getString("lastSSID", "");
  preferences.end();
  
  if (lastConnectedSSID.length() > 0) {
    Serial.println("📱 Last connected to: " + lastConnectedSSID);
  }
}

void savePreferences() {
  preferences.begin("jlink-pos", false);
  preferences.putInt("scanCount", scanCount);
  if (WiFi.isConnected()) {
    preferences.putString("lastSSID", WiFi.SSID());
  }
  preferences.end();
}

// ==================== MAIN LOOP ====================
void loop() {
  // Check for long press on config button (3 seconds)
  if (digitalRead(CONFIG_BUTTON) == LOW) {
    if (configButtonPressTime == 0) {
      configButtonPressTime = millis();
    } else if (millis() - configButtonPressTime > 3000) {
      Serial.println("⚙️ Config button long press detected");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ENTERING CONFIG");
      lcd.setCursor(0, 1);
      lcd.print("MODE...");
      beep(2);
      delay(1000);
      enterConfigMode();
      configButtonPressTime = 0;
    }
  } else {
    configButtonPressTime = 0;
  }
  
  handleQRScanner();
  
  // Check WiFi connection every 30 seconds
  if (millis() - lastWifiCheck > 30000) {
    checkWiFiConnection();
    lastWifiCheck = millis();
  }
  
  updateStatusDisplay();
  delay(100);
}

// ==================== WIFI CONFIGURATION MODE ====================
void enterConfigMode() {
  configMode = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONFIG MODE");
  lcd.setCursor(0, 1);
  lcd.print("Connect to AP");
  
  Serial.println("📡 Starting Configuration Portal");
  Serial.println("📱 Connect to WiFi: 'JLINK-POS-Config'");
  Serial.println("🔒 Password: 'jlinkpos06'");
  Serial.println("🌐 Configure at: http://192.168.4.1");
  
  beep(2);
  
  // Reset settings if needed (uncomment for testing)
  // wm.resetSettings();
  
  // Set custom timeouts
  wm.setConfigPortalTimeout(180); // 3 minutes timeout
  wm.setConnectTimeout(20); // 20 seconds to connect
  wm.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  
  // FIX: Create menu vector properly
  std::vector<const char *> menu = {"wifi", "info", "exit"};
  wm.setMenu(menu);
  
  // Set custom portal settings
  wm.setClass("invert"); // Dark theme
  wm.setShowInfoUpdate(false);
  wm.setShowInfoErase(true);
  
  // Set callback for when connecting to WiFi
  wm.setSaveConfigCallback(saveConfigCallback);
  
  // Start configuration portal with custom AP name and password
  bool res = wm.startConfigPortal("JLINK-POS-Config", "jlinkpos06");
  
  if (res) {
    Serial.println("✅ Connected to WiFi!");
    wifiConnected = true;
    savePreferences();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("CONNECTED!");
    lcd.setCursor(0, 1);
    String ssid = WiFi.SSID();
    if (ssid.length() > 16) {
      ssid = ssid.substring(0, 16);
    }
    lcd.print(ssid);
    
    digitalWrite(STATUS_LED, HIGH);
    beep(2);
    delay(2000);
    
    // Test Vercel connection
    testVercelConnection();
  } else {
    Serial.println("❌ Configuration failed or timeout");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("CONFIG TIMEOUT");
    lcd.setCursor(0, 1);
    lcd.print("Restarting...");
    beep(3);
    delay(2000);
    ESP.restart();
  }
  
  configMode = false;
}

// Callback function for WiFiManager
void saveConfigCallback() {
  Serial.println("✅ WiFi credentials saved!");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WIFI SAVED!");
  lcd.setCursor(0, 1);
  lcd.print("Connecting...");
}

// ==================== TEST VERCEL CONNECTION ====================
void testVercelConnection() {
  Serial.println("🧪 Testing Vercel connection...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Testing Vercel");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");
  
  HTTPClient http;
  http.begin(testAPI);
  http.setTimeout(15000);
  
  Serial.println("📡 Connecting to: " + testAPI);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("✅ Vercel Test Response:");
    Serial.println(response);
    
    // Parse JSON response
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      String status = doc["status"] | "unknown";
      String message = doc["message"] | "Connected";
      
      Serial.println("📊 Status: " + status);
      Serial.println("💬 Message: " + message);
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("VERCEL ONLINE!");
      lcd.setCursor(0, 1);
      lcd.print("System Ready");
      beep(2);
      delay(2000);
    }
  } else if (httpCode > 0) {
    Serial.println("⚠️ Vercel Test - HTTP Code: " + String(httpCode));
    Serial.println("Response: " + http.getString());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VERCEL WARNING");
    lcd.setCursor(0, 1);
    lcd.print("Code: " + String(httpCode));
    beep(2);
    delay(2000);
  } else {
    Serial.println("❌ Vercel Test Failed!");
    Serial.println("Error: " + http.errorToString(httpCode));
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VERCEL ERROR!");
    lcd.setCursor(0, 1);
    lcd.print("Check URL");
    beep(3);
    delay(3000);
  }
  
  http.end();
}

// ==================== QR SCANNER HANDLING ====================
void handleQRScanner() {
  if (Serial2.available()) {
    String qrData = Serial2.readStringUntil('\n');
    qrData.trim();
    
    if (qrData.length() > 0 && qrData.length() < 200) {
      Serial.println("📱 QR Code Scanned: " + qrData);
      lastQRScan = millis();
      scanCount++;
      savePreferences();
      
      // Extract product code from QR data
      currentProductCode = extractProductCode(qrData);
      
      // Show on LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("PRODUCT SCANNED");
      lcd.setCursor(0, 1);
      
      String displayCode = currentProductCode;
      if (displayCode.length() > 15) {
        displayCode = displayCode.substring(0, 15);
      }
      lcd.print(displayCode);
      
      // Send to Vercel API
      processQRCode(qrData);
    }
  }
}

String extractProductCode(String qrData) {
  // Handle different QR code formats
  
  // Format 1: Pipe-delimited (Name|Price|MFD|EXP|ProductCode)
  int lastPipe = qrData.lastIndexOf('|');
  if (lastPipe != -1 && lastPipe < qrData.length() - 1) {
    String code = qrData.substring(lastPipe + 1);
    code.trim();
    if (code.length() > 0) {
      return code;
    }
  }
  
  // Format 2: PROD: prefix
  if (qrData.indexOf("PROD:") != -1) {
    int startPos = qrData.indexOf("PROD:") + 5;
    int endPos = qrData.indexOf('|', startPos);
    if (endPos == -1) endPos = qrData.indexOf('\n', startPos);
    if (endPos == -1) endPos = qrData.length();
    String code = qrData.substring(startPos, endPos);
    code.trim();
    if (code.length() > 0) {
      return code;
    }
  }
  
  // Format 3: Plain code
  return qrData;
}

// ==================== PROCESS QR CODE ====================
void processQRCode(String qrCode) {
  if (!wifiConnected) {
    showErrorOnLCD("NO WIFI!");
    beep(3);
    delay(2000);
    return;
  }
  
  digitalWrite(STATUS_LED, HIGH);
  
  HTTPClient http;
  bool success = false;
  
  Serial.println("📡 Sending to Vercel: " + scannerAPI);
  
  http.begin(scannerAPI);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(15000);
  
  // Create JSON payload
  DynamicJsonDocument doc(512);
  doc["qr_code"] = qrCode;
  doc["scanner_id"] = "ESP32_GM67";
  doc["timestamp"] = millis();
  doc["product_code"] = currentProductCode;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("📦 Sending JSON: " + jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  Serial.println("📡 Response Code: " + String(httpResponseCode));
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    Serial.println("📡 Response: " + response);
    
    DynamicJsonDocument responseDoc(1024);
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (!error && responseDoc["success"] == true) {
      success = true;
      String message = responseDoc["message"] | "Sent to POS";
      Serial.println("✅ " + message);
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("SENT TO POS!");
      lcd.setCursor(0, 1);
      lcd.print("Scan #" + String(scanCount));
      
      beep(2);
    } else {
      Serial.println("❌ Server error");
      if (error) {
        Serial.println("JSON parse error: " + String(error.c_str()));
      } else {
        String errorMsg = responseDoc["error"] | "Unknown error";
        Serial.println("Error: " + errorMsg);
      }
      showErrorOnLCD("SERVER ERROR");
    }
  } else if (httpResponseCode > 0) {
    Serial.println("❌ HTTP Error: " + String(httpResponseCode));
    String response = http.getString();
    Serial.println("Response: " + response);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("HTTP ERROR");
    lcd.setCursor(0, 1);
    lcd.print("Code: " + String(httpResponseCode));
    
    success = false;
  } else {
    Serial.println("❌ Connection Error: " + http.errorToString(httpResponseCode));
    
    showErrorOnLCD("NO CONNECTION");
    Serial.println("⚠️ Check WiFi connection");
    Serial.println("⚠️ Check Vercel URL: " + VERCEL_URL);
  }
  
  http.end();
  digitalWrite(STATUS_LED, LOW);
  
  if (!success) {
    beep(3);
    delay(3000);
  } else {
    delay(2000);
  }
}

// ==================== LCD FUNCTIONS ====================
void initializeLCD() {
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  Serial.println("✅ LCD Initialized");
}

void showStartupMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" JLINK POS v5.0 ");
  lcd.setCursor(0, 1);
  lcd.print("VERCEL MODE");
  delay(2000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hold BOOT for");
  lcd.setCursor(0, 1);
  lcd.print("WiFi Config");
  delay(2000);
}

void showErrorOnLCD(String error) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ERROR:");
  lcd.setCursor(0, 1);
  
  if (error.length() > 15) {
    error = error.substring(0, 15);
  }
  lcd.print(error);
}

void updateStatusDisplay() {
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate < 3000) return;
  lastDisplayUpdate = millis();
  
  // Don't update if we just scanned something
  if (millis() - lastQRScan < 4000) return;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  
  if (wifiConnected) {
    lcd.print("ONLINE - READY");
    lcd.setCursor(0, 1);
    String ssid = WiFi.SSID();
    if (ssid.length() > 16) {
      ssid = ssid.substring(0, 16);
    }
    lcd.print(ssid);
  } else {
    lcd.print("OFFLINE!");
    lcd.setCursor(0, 1);
    lcd.print("Check WiFi");
  }
}

// ==================== WIFI FUNCTIONS ====================
void connectToWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONNECTING WiFi");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");
  
  Serial.println("📡 Attempting WiFi connection...");
  
  // Set WiFiManager timeouts
  wm.setConnectTimeout(20); // 20 seconds timeout for connecting
  wm.setConfigPortalTimeout(0); // No timeout in config portal mode
  
  // Disable debug output (reduce serial spam)
  wm.setDebugOutput(true);
  
  // Try to auto-connect using saved credentials
  // If it fails, it will start a config portal
  bool res = wm.autoConnect("JLINK-POS-Config", "jlinkpos123");
  
  if (res) {
    wifiConnected = true;
    Serial.println("✅ WiFi Connected!");
    Serial.println("📶 SSID: " + WiFi.SSID());
    Serial.println("📶 IP: " + WiFi.localIP().toString());
    Serial.println("📶 Signal: " + String(WiFi.RSSI()) + " dBm");
    
    savePreferences();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WI-FI CONNECTED");
    lcd.setCursor(0, 1);
    String ssid = WiFi.SSID();
    if (ssid.length() > 16) {
      ssid = ssid.substring(0, 16);
    }
    lcd.print(ssid);
    
    digitalWrite(STATUS_LED, HIGH);
    beep(2);
    delay(2000);
    
  } else {
    wifiConnected = false;
    Serial.println("❌ WiFi Connection Failed!");
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WI-FI FAILED!");
    lcd.setCursor(0, 1);
    lcd.print("Press BOOT");
    
    digitalWrite(STATUS_LED, LOW);
    beep(3);
    
    Serial.println("⚙️ Press BOOT button for 3 seconds to enter config mode");
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    Serial.println("⚠️ WiFi disconnected! Reconnecting...");
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RECONNECTING");
    lcd.setCursor(0, 1);
    lcd.print("WiFi...");
    
    WiFi.reconnect();
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(1000);
      Serial.print(".");
      attempts++;
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.println("\n✅ Reconnected!");
      digitalWrite(STATUS_LED, HIGH);
    } else {
      Serial.println("\n❌ Reconnection failed");
      showErrorOnLCD("NO WIFI!");
      digitalWrite(STATUS_LED, LOW);
    }
  }
}

// ==================== AUDIO FEEDBACK ====================
void beep(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(100);
  }
}
