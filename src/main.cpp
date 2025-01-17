#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// Konfigurasi Access Point
const char* ssid = "Kijang1";
const char* password = "password1";  // Minimal 8 karakter untuk WPA2

// DNS Server untuk captive portal
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer server(80);

// Konfigurasi LCD dan sensor
LiquidCrystal_I2C lcd(0x27,20,4);
const int echoPinA = 14;
const int trigPinA = 27;
const int echoPinB = 26;
const int trigPinB = 25;

// Variabel counting
int countIn = 0;
int countOut = 0;
int currentVisitor = 0;

// Konstanta untuk sistem deteksi
const int detectionThreshold = 30;     // Jarak deteksi dalam cm
const int consecutiveReadings = 2;     // Jumlah pembacaan berturut-turut untuk konfirmasi
const int sensorTimeout = 1000;        // Timeout untuk menunggu sensor kedua (ms)
const int debounceDelay = 250;         // Delay minimum antara deteksi (ms)

// Status sensor
bool sensorAState = false;
bool sensorBState = false;
unsigned long lastDetectionTime = 0;

// Buffer untuk pembacaan berturut-turut
int bufferA[consecutiveReadings];
int bufferB[consecutiveReadings];
int bufferIndex = 0;

// HTML template
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Visitor Counter System</title>
  <style>
    body { 
      font-family: Arial; 
      text-align: center; 
      margin: 0;
      padding: 20px;
      background-color: #f0f0f0;
    }
    .card {
      background-color: white;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
      margin: 10px auto;
      max-width: 400px;
    }
    .value {
      font-size: 30px;
      font-weight: bold;
      color: #0066cc;
    }
    .label {
      font-size: 16px;
      color: #666;
    }
    .sensor-data {
      font-size: 14px;
      color: #888;
      margin-top: 20px;
    }
  </style>
  <script>
    setInterval(function() {
      getData();
    }, 1000);

    function getData() {
      var xhttp = new XMLHttpRequest();
      xhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          var data = JSON.parse(this.responseText);
          document.getElementById("countIn").innerHTML = data.in;
          document.getElementById("countOut").innerHTML = data.out;
          document.getElementById("current").innerHTML = data.current;
          document.getElementById("sensorA").innerHTML = data.distanceA;
          document.getElementById("sensorB").innerHTML = data.distanceB;
        }
      };
      xhttp.open("GET", "/data", true);
      xhttp.send();
    }
  </script>
</head>
<body>
  <h2>Visitor Counter System</h2>
  
  <div class="card">
    <div class="label">Masuk</div>
    <div class="value" id="countIn">0</div>
  </div>
  
  <div class="card">
    <div class="label">Keluar</div>
    <div class="value" id="countOut">0</div>
  </div>
  
  <div class="card">
    <div class="label">Pengunjung Saat Ini</div>
    <div class="value" id="current">0</div>
  </div>

  <div class="sensor-data">
    <p>Sensor A: <span id="sensorA">0</span> cm</p>
    <p>Sensor B: <span id="sensorB">0</span> cm</p>
  </div>
</body>
</html>
)rawliteral";

// Fungsi untuk membaca jarak dari sensor ultrasonik dengan filter noise
long readDistance(int trigPin, int echoPin) {
  // Lakukan 3 pembacaan cepat dan ambil rata-rata
  long total = 0;
  for(int i = 0; i < 3; i++) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    long duration = pulseIn(echoPin, HIGH, 23529); // Timeout untuk jarak maksimal 4 meter
    if (duration == 0) return 400; // Return nilai maksimal jika timeout
    
    total += duration * 0.034 / 2;
    delay(10); // Delay kecil antara pembacaan
  }
  return total / 3;
}

// Fungsi untuk mengecek apakah sensor mendeteksi objek dengan konsisten
bool isObjectDetected(int buffer[], long newReading) {
  // Geser nilai dalam buffer
  for(int i = 0; i < consecutiveReadings-1; i++) {
    buffer[i] = buffer[i+1];
  }
  buffer[consecutiveReadings-1] = (newReading < detectionThreshold) ? 1 : 0;
  
  // Cek apakah semua pembacaan menunjukkan deteksi
  int sum = 0;
  for(int i = 0; i < consecutiveReadings; i++) {
    sum += buffer[i];
  }
  return sum >= consecutiveReadings-1; // Toleransi 1 pembacaan gagal
}

// Handler untuk root path
void handleRoot() {
  server.send(200, "text/html", index_html);
}

// Handler untuk data JSON
void handleData() {
  long distanceA = readDistance(trigPinA, echoPinA);
  long distanceB = readDistance(trigPinB, echoPinB);
  
  String json = "{";
  json += "\"in\":" + String(countIn) + ",";
  json += "\"out\":" + String(countOut) + ",";
  json += "\"current\":" + String(currentVisitor) + ",";
  json += "\"distanceA\":" + String(distanceA) + ",";
  json += "\"distanceB\":" + String(distanceB);
  json += "}";
  server.send(200, "application/json", json);
}

// Handler untuk halaman tidak ditemukan
void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// Setup web server
void setupServer() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, password);

  // DNS Server untuk redirect semua request ke ESP32
  dnsServer.start(DNS_PORT, "*", apIP);

  // Route handler
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("HTTP server started");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  // Setup pin sensor
  pinMode(trigPinA, OUTPUT);
  pinMode(echoPinA, INPUT);
  pinMode(trigPinB, OUTPUT);
  pinMode(echoPinB, INPUT);

  // Inisialisasi buffer
  memset(bufferA, 0, sizeof(bufferA));
  memset(bufferB, 0, sizeof(bufferB));

  Serial.begin(115200);
  
  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Visitor Counter");
  lcd.setCursor(0, 1);
  lcd.print("Starting AP...");

  // Setup Web Server
  setupServer();
  
  delay(2000);
  lcd.clear();
}

void loop() {
  // Handle DNS dan Web Server
  dnsServer.processNextRequest();
  server.handleClient();

  // Baca jarak dari kedua sensor
  long distanceA = readDistance(trigPinA, echoPinA);
  long distanceB = readDistance(trigPinB, echoPinB);

  // Update LCD dengan jarak
  lcd.setCursor(0, 0);
  lcd.print("A:");
  lcd.print(distanceA);
  lcd.print("cm ");
  lcd.print("B:");
  lcd.print(distanceB);
  lcd.print("cm    ");

  // Cek waktu debounce
  if (millis() - lastDetectionTime < debounceDelay) {
    return;
  }

  // Deteksi status sensor dengan filter
  bool newSensorAState = isObjectDetected(bufferA, distanceA);
  bool newSensorBState = isObjectDetected(bufferB, distanceB);

  // State machine untuk mendeteksi arah
  static enum {IDLE, A_ACTIVE, B_ACTIVE} state = IDLE;
  static unsigned long stateTimer = 0;

  switch(state) {
    case IDLE:
      if (newSensorAState && !newSensorBState) {
        state = A_ACTIVE;
        stateTimer = millis();
      }
      else if (newSensorBState && !newSensorAState) {
        state = B_ACTIVE;
        stateTimer = millis();
      }
      break;

    case A_ACTIVE:
      if (newSensorBState) {
        if (millis() - stateTimer < sensorTimeout) {
          countIn++;
          currentVisitor++;
          Serial.println("IN: " + String(countIn));
          lastDetectionTime = millis();
        }
        state = IDLE;
      }
      else if (millis() - stateTimer > sensorTimeout) {
        state = IDLE;
      }
      break;

    case B_ACTIVE:
      if (newSensorAState) {
        if (millis() - stateTimer < sensorTimeout) {
          countOut++;
          currentVisitor = max(0, currentVisitor - 1);
          Serial.println("OUT: " + String(countOut));
          lastDetectionTime = millis();
        }
        state = IDLE;
      }
      else if (millis() - stateTimer > sensorTimeout) {
        state = IDLE;
      }
      break;
  }

  // Update LCD
  lcd.setCursor(0, 1);
  lcd.print("I:");
  lcd.print(countIn);
  lcd.print(" O:");
  lcd.print(countOut);
  lcd.print(" C:");
  lcd.print(currentVisitor);
  lcd.print("   ");

  // Delay kecil untuk stabilitas dan mencegah watchdog reset
  delay(50);
}

// #include <Arduino.h>
// #include <Wire.h>
// #include <LiquidCrystal_I2C.h>

// // Konfigurasi layar LCD I2C
// LiquidCrystal_I2C lcd(0x27,20,4);

// // Pin konfigurasi untuk Sensor A dan Sensor B
// const int trigPinA = 14;
// const int echoPinA = 27;
// const int trigPinB = 26;
// const int echoPinB = 25;

// // Variabel counting
// int countIn = 0;
// int countOut = 0;
// int currentVisitor = 0;

// // Konstanta untuk sistem deteksi
// const int detectionThreshold = 20;     // Jarak deteksi dalam cm
// const int consecutiveReadings = 3;     // Jumlah pembacaan berturut-turut untuk konfirmasi
// const int sensorTimeout = 1500;        // Timeout untuk menunggu sensor kedua (ms)
// const int debounceDelay = 500;         // Delay minimum antara deteksi (ms)

// // Status sensor
// bool sensorAState = false;
// bool sensorBState = false;
// unsigned long lastDetectionTime = 0;

// // Buffer untuk pembacaan berturut-turut
// int bufferA[consecutiveReadings];
// int bufferB[consecutiveReadings];
// int bufferIndex = 0;

// // Fungsi untuk membaca jarak dari sensor ultrasonik dengan filter noise
// long readDistance(int trigPin, int echoPin) {
//   // Lakukan 3 pembacaan cepat dan ambil rata-rata
//   long total = 0;
//   for(int i = 0; i < 3; i++) {
//     digitalWrite(trigPin, LOW);
//     delayMicroseconds(2);
//     digitalWrite(trigPin, HIGH);
//     delayMicroseconds(10);
//     digitalWrite(trigPin, LOW);

//     long duration = pulseIn(echoPin, HIGH, 23529); // Timeout untuk jarak maksimal 4 meter
//     if (duration == 0) return 400; // Return nilai maksimal jika timeout
    
//     total += duration * 0.034 / 2;
//     delay(10); // Delay kecil antara pembacaan
//   }
//   return total / 3;
// }

// // Fungsi untuk mengecek apakah sensor mendeteksi objek dengan konsisten
// bool isObjectDetected(int buffer[], long newReading) {
//   // Geser nilai dalam buffer
//   for(int i = 0; i < consecutiveReadings-1; i++) {
//     buffer[i] = buffer[i+1];
//   }
//   buffer[consecutiveReadings-1] = (newReading < detectionThreshold) ? 1 : 0;
  
//   // Cek apakah semua pembacaan menunjukkan deteksi
//   int sum = 0;
//   for(int i = 0; i < consecutiveReadings; i++) {
//     sum += buffer[i];
//   }
//   return sum >= consecutiveReadings-1; // Toleransi 1 pembacaan gagal
// }

// void setup() {
//   pinMode(trigPinA, OUTPUT);
//   pinMode(echoPinA, INPUT);
//   pinMode(trigPinB, OUTPUT);
//   pinMode(echoPinB, INPUT);

//   // Inisialisasi buffer
//   memset(bufferA, 0, sizeof(bufferA));
//   memset(bufferB, 0, sizeof(bufferB));

//   Serial.begin(115200); // Gunakan baud rate lebih tinggi
//   Serial.println("Improved Counting Gate System Initialized");

//   lcd.init();
//   lcd.backlight();
//   lcd.setCursor(0, 0);
//   lcd.print("Improved Counter");
//   lcd.setCursor(0, 1);
//   lcd.print("Initializing...");
//   delay(2000);
//   lcd.clear();
// }

// void loop() {
//   // Baca jarak dari kedua sensor
//   long distanceA = readDistance(trigPinA, echoPinA);
//   long distanceB = readDistance(trigPinB, echoPinB);

//   // Update LCD dengan jarak
//   lcd.setCursor(0, 0);
//   lcd.print("A:");
//   lcd.print(distanceA);
//   lcd.print("cm ");
//   lcd.print("B:");
//   lcd.print(distanceB);
//   lcd.print("cm    ");

//   // Cek waktu debounce
//   if (millis() - lastDetectionTime < debounceDelay) {
//     return;
//   }

//   // Deteksi status sensor dengan filter
//   bool newSensorAState = isObjectDetected(bufferA, distanceA);
//   bool newSensorBState = isObjectDetected(bufferB, distanceB);

//   // State machine untuk mendeteksi arah
//   static enum {IDLE, A_ACTIVE, B_ACTIVE} state = IDLE;
//   static unsigned long stateTimer = 0;

//   switch(state) {
//     case IDLE:
//       if (newSensorAState && !newSensorBState) {
//         state = A_ACTIVE;
//         stateTimer = millis();
//       }
//       else if (newSensorBState && !newSensorAState) {
//         state = B_ACTIVE;
//         stateTimer = millis();
//       }
//       break;

//     case A_ACTIVE:
//       if (newSensorBState) {
//         if (millis() - stateTimer < sensorTimeout) {
//           countIn++;
//           currentVisitor++;
//           Serial.println("IN: " + String(countIn));
//           lastDetectionTime = millis();
//         }
//         state = IDLE;
//       }
//       else if (millis() - stateTimer > sensorTimeout) {
//         state = IDLE;
//       }
//       break;

//     case B_ACTIVE:
//       if (newSensorAState) {
//         if (millis() - stateTimer < sensorTimeout) {
//           countOut++;
//           currentVisitor = max(0, currentVisitor - 1);
//           Serial.println("OUT: " + String(countOut));
//           lastDetectionTime = millis();
//         }
//         state = IDLE;
//       }
//       else if (millis() - stateTimer > sensorTimeout) {
//         state = IDLE;
//       }
//       break;
//   }

//   // Update LCD
//   lcd.setCursor(0, 1);
//   lcd.print("I:");
//   lcd.print(countIn);
//   lcd.print(" O:");
//   lcd.print(countOut);
//   lcd.print(" C:");
//   lcd.print(currentVisitor);
//   lcd.print("   ");

//   delay(50); // Delay kecil untuk stabilitas
// }
