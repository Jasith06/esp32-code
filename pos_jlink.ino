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
// ⚠️ IMPORTANT: Replace with YOUR computer's local IP address ⚠️
// To find your IP: Open CMD and type: ipconfig
// Look for "IPv4 Address" under your active network adapter
const String serverIP = "192.168.8.80"; // CHANGE THIS TO YOUR COMPUTER'S IP!

const String serverBaseURL = "http://" + serverIP + "/jlink-pos-web";
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
  testXAMPPConnection();
  
  Serial.println("🚀 JLINK POS Scanner System Ready!");
  Serial.println("📡 Server: " + serverBaseURL);
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

// ==================== TEST XAMPP CONNECTION ====================
void testXAMPPConnection() {
  Serial.println("🧪 Testing XAMPP connection...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Testing XAMPP");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");
  
  HTTPClient http;
  http.begin(testAPI);
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("✅ XAMPP Test Response:");
    Serial.println(response);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("XAMPP CONNECTED");
    lcd.setCursor(0, 1);
    lcd.print("System Ready!");
    beep(2);
    delay(2000);
  } else {
    Serial.println("❌ XAMPP Test Failed!");
    Serial.println("Error Code: " + String(httpCode));
    Serial.println("Error: " + http.errorToString(httpCode));
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("XAMPP ERROR!");
    lcd.setCursor(0, 1);
    lcd.print("Check server");
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
      
      // Send to XAMPP server
      processQRCode(qrData);
    }
  }
}

String extractProductCode(String qrData) {
  // Python QR format: Name|Price|MFD|EXP|ProductCode
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
  
  Serial.println("📡 Sending to XAMPP: " + scannerAPI);
  
  http.begin(scannerAPI);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(15000);
  
  // Create JSON payload
  DynamicJsonDocument doc(512);
  doc["qr_code"] = qrCode;
  doc["scanner_id"] = "ESP32_GM67";
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("📦 Sending JSON: " + jsonString);
  
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
      if (error) {
        Serial.println("JSON parse error: " + String(error.c_str()));
      }
      showErrorOnLCD("SERVER ERROR");
    }
  } else {
    Serial.println("❌ HTTP Error: " + String(httpResponseCode));
    Serial.println("❌ Error: " + http.errorToString(httpResponseCode));
    
    if (httpResponseCode == -1) {
      showErrorOnLCD("NO CONNECTION");
      Serial.println("⚠️ Check if XAMPP Apache is running!");
      Serial.println("⚠️ Check if IP address is correct: " + serverIP);
    } else if (httpResponseCode == 404) {
      showErrorOnLCD("FILE NOT FOUND");
      Serial.println("⚠️ Check if files are in: C:/xampp/htdocs/jlink-pos-web/");
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
  lcd.print(" JLINK POS v4.0 ");
  lcd.setCursor(0, 1);
  lcd.print("XAMPP MODE");
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
    Serial.println("🖥️  Server IP: " + serverIP);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WI-FI CONNECTED");
    lcd.setCursor(0, 1);
    lcd.print("IP: " + WiFi.localIP().toString().substring(0, 15));
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
