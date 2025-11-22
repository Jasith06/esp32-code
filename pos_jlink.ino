#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// ==================== HARDWARE CONFIGURATION ====================
#define QR_SCANNER_RX 16
#define QR_SCANNER_TX 17
#define LCD_I2C_ADDRESS 0x27
#define LCD_COLS 16
#define LCD_ROWS 2
#define STATUS_LED 2
#define BUZZER_PIN 4

// ==================== WIFI CONFIGURATION ====================
const char* ssid = "SCAM ALERT";
const char* password = "Fakemode@06";

// ==================== XAMPP SERVER CONFIGURATION ====================
// ⚠️ YOUR COMPUTER'S IP ADDRESS ⚠️
const String serverIP = "192.168.8.101"; // YOUR COMPUTER IP
const String serverBaseURL = "http://" + serverIP + "/jlink-pos";
const String scannerAPI = serverBaseURL + "/api/scanner.php";
const String testAPI = serverBaseURL + "/api/test.php";

// ==================== GLOBAL OBJECTS ====================
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLS, LCD_ROWS);

// ==================== VARIABLES ====================
unsigned long lastWifiCheck = 0;
unsigned long lastQRScan = 0;
bool wifiConnected = false;
int scanCount = 0;
String currentProductCode = "";

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, QR_SCANNER_RX, QR_SCANNER_TX);
  
  initializeLCD();
  initializePins();
  showStartupMessage();
  connectToWiFi();
  
  Serial.println("🚀 JLINK POS Scanner System Ready!");
  beep(1);
}

void initializePins() {
  pinMode(STATUS_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
}

// ==================== MAIN LOOP ====================
void loop() {
  handleQRScanner();
  
  if (millis() - lastWifiCheck > 30000) {
    checkWiFiConnection();
    lastWifiCheck = millis();
  }
  
  updateStatusDisplay();
  delay(100);
}

// ==================== QR SCANNER HANDLING ====================
void handleQRScanner() {
  if (Serial2.available()) {
    String qrData = Serial2.readStringUntil('\n');
    qrData.trim();
    
    if (qrData.length() > 0 && qrData.length() < 100) {
      Serial.println("📱 QR Code Scanned: " + qrData);
      lastQRScan = millis();
      scanCount++;
      
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
      
      // Send to server
      processQRCode(qrData);
    }
  }
}

String extractProductCode(String qrData) {
  // Your Python QR format: Name|Price|MFD|EXP|ProductCode
  int lastPipe = qrData.lastIndexOf('|');
  if (lastPipe != -1) {
    return qrData.substring(lastPipe + 1);
  }
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
  
  Serial.println("📡 Sending to: " + scannerAPI);
  
  http.begin(scannerAPI);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(15000); // 15 second timeout
  
  // Create JSON payload
  DynamicJsonDocument doc(512);
  doc["qr_code"] = qrCode;
  doc["scanner_id"] = "ESP32_GM67";
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("📦 Sending data...");
  
  int httpResponseCode = http.POST(jsonString);
  
  Serial.println("📡 Response Code: " + String(httpResponseCode));
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    Serial.println("📡 Response: " + response);
    
    DynamicJsonDocument responseDoc(512);
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (!error && responseDoc["success"] == true) {
      success = true;
      Serial.println("✅ Sent to POS successfully!");
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("SENT TO POS!");
      lcd.setCursor(0, 1);
      lcd.print("Scan #" + String(scanCount));
      
      beep(2);
    } else {
      Serial.println("❌ Server error");
      showErrorOnLCD("SERVER ERROR");
    }
  } else {
    Serial.println("❌ HTTP Error: " + String(httpResponseCode));
    Serial.println("❌ Error: " + http.errorToString(httpResponseCode));
    
    if (httpResponseCode == -1) {
      showErrorOnLCD("NO CONNECTION");
    } else {
      showErrorOnLCD("HTTP ERROR");
    }
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
  lcd.print(" JLINK POS v3.0 ");
  lcd.setCursor(0, 1);
  lcd.print("  INITIALIZING...");
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
  if (millis() - lastDisplayUpdate < 2000) return;
  lastDisplayUpdate = millis();
  
  if (millis() - lastQRScan < 4000) return;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  
  if (wifiConnected) {
    lcd.print("READY - ONLINE");
  } else {
    lcd.print("READY - OFFLINE");
  }
  
  lcd.setCursor(0, 1);
  lcd.print("Scans: " + String(scanCount));
}

// ==================== WIFI FUNCTIONS ====================
String getConnectingDots(int attempts) {
  int dotCount = attempts % 4;
  String dots = "";
  for (int i = 0; i < dotCount; i++) {
    dots += ".";
  }
  return dots;
}

void connectToWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CONNECTING WiFi");
  
  Serial.println("📡 Connecting to: " + String(ssid));
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    attempts++;
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    
    lcd.setCursor(0, 1);
    lcd.print("Connecting" + getConnectingDots(attempts));
    lcd.print("    ");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✅ WiFi Connected!");
    Serial.println("📶 IP: " + WiFi.localIP().toString());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WI-FI CONNECTED");
    lcd.setCursor(0, 1);
    lcd.print("Ready to scan!");
    digitalWrite(STATUS_LED, HIGH);
    delay(2000);
    
  } else {
    wifiConnected = false;
    Serial.println("\n❌ WiFi Failed!");
    showErrorOnLCD("WI-FI FAILED");
    digitalWrite(STATUS_LED, LOW);
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    connectToWiFi();
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