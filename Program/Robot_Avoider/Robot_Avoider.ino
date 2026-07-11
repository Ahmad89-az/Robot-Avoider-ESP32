#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

// ============================================================================
// KONFIGURASI WIFI - SESUAIKAN
// ============================================================================
const char* WIFI_SSID     = "SIMETRI";
const char* WIFI_PASSWORD = "1234567890";

// ============================================================================
// KONFIGURASI MQTT
// ============================================================================
const char* MQTT_BROKER = "broker.emqx.io";
const int   MQTT_PORT   = 1883;

// Serial number perangkat - UBAH DI SINI sesuai kebutuhan (unik per unit)
char deviceSerialNumber[32] = "SN001";

// Buffer topic MQTT, dibentuk otomatis dari serial number saat setup()
char mqttTopic[64];

const unsigned long MQTT_PUBLISH_INTERVAL_MS = 60000; // publish tiap 1 detik

// ============================================================================
// KONFIGURASI PIN
// ============================================================================
#define PIN_BUTTON_FX 27   // Tombol fungsi, kaki lain ke GND (pakai INPUT_PULLUP)
#define PIN_DS18B20   4
#define PIN_DHT22     15

OneWire oneWireBus(PIN_DS18B20); 
DallasTemperature waterTempSensor(&oneWireBus);
DHT dhtSensor(PIN_DHT22, DHT22);

// ============================================================================
// KONFIGURASI LCD
// ============================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
const unsigned long LCD_IDLE_TIMEOUT_MS = 60000; // LCD mati setelah 1 menit idle
bool lcdIsOn = true;
unsigned long lastActivityMillis = 0;

// ============================================================================
// OBJEK WIFI & MQTT
// ============================================================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ============================================================================
// STRUKTUR DATA SENSOR (SIMULASI)
// ============================================================================
struct SensorData {
  float phValue;
  float tdsValuePpm;
  float waterTempC;
  float airTempC;
  float airHumidityPct;
} sensorData;

// ============================================================================
// TIMER NON-BLOCKING
// ============================================================================
unsigned long lastPublishMillis = 0;
unsigned long lastLcdUpdateMillis = 0;
const unsigned long LCD_UPDATE_INTERVAL_MS = 3000;
uint8_t currentLcdPage = 0; // 0=pH, 1=TDS, 2=Suhu Air, 3=Suhu Udara, 4=Kelembapan

// Debounce tombol
unsigned long lastButtonPressMillis = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 200;
bool lastButtonState = HIGH;

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0)); // seed random dari noise ADC

  pinMode(PIN_BUTTON_FX, INPUT_PULLUP);
  waterTempSensor.begin();
  dhtSensor.begin();

  Wire.begin(21, 22);
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("Hydroponic Sys"));
  lcd.setCursor(0, 1);
  lcd.print(F("Connecting."));

  buildMqttTopic();
  connectWiFi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);

  lastActivityMillis = millis();
  lcd.clear();
}

// ============================================================================
// LOOP UTAMA
// ============================================================================
void loop() {
  unsigned long currentMillis = millis();

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  checkButton(currentMillis);
  checkLcdIdleTimeout(currentMillis);

  // --- Baca sensor & publish tiap 1 detik ---
  if (currentMillis - lastPublishMillis >= MQTT_PUBLISH_INTERVAL_MS) {
    lastPublishMillis = currentMillis;
    generateRandomPHTDS();
    readWaterTemp();
    readDHT();
    publishSensorData();
  }

  // --- Update tampilan LCD tiap 3 detik (jika sedang ON) ---
  if (lcdIsOn && (currentMillis - lastLcdUpdateMillis >= LCD_UPDATE_INTERVAL_MS)) {
    lastLcdUpdateMillis = currentMillis;
    displayLCD(currentLcdPage);
    currentLcdPage = (currentLcdPage + 1) % 5;
  }
}

// ============================================================================
// FUNGSI: buildMqttTopic()
// Membentuk topic MQTT dari serial number: simetri/data/<serial_number>
// ============================================================================
void buildMqttTopic() {
  snprintf(mqttTopic, sizeof(mqttTopic), "simetri/data/%s", deviceSerialNumber);
  Serial.print(F("MQTT Topic: "));
  Serial.println(mqttTopic);
}

// ============================================================================
// FUNGSI: connectWiFi()
// Menghubungkan ke WiFi, menunggu hingga terkoneksi.
// ============================================================================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print(F("Menghubungkan ke WiFi: "));
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttemptMillis = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptMillis < 15000) {
    delay(300); // hanya dipakai saat proses koneksi awal, bukan pada logika utama
    Serial.print(F("."));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print(F("WiFi terhubung, IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println(F("[WARNING] Gagal konek WiFi, akan dicoba lagi."));
  }
}

// ============================================================================
// FUNGSI: reconnectMQTT()
// Menghubungkan ulang ke broker MQTT jika terputus.
// ============================================================================
void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.print(F("Menghubungkan ke MQTT broker..."));

  // Client ID unik berbasis serial number
  char clientId[48];
  snprintf(clientId, sizeof(clientId), "esp32-%s", deviceSerialNumber);

  if (mqttClient.connect(clientId)) {
    Serial.println(F(" terhubung."));
  } else {
    Serial.print(F(" gagal, rc="));
    Serial.print(mqttClient.state());
    Serial.println(F(" - coba lagi nanti."));
  }
}

// ============================================================================
// FUNGSI: generateRandomPHTDS()
// pH dan TDS masih SIMULASI (random) - ganti dengan pembacaan sensor asli
// saat hardware pH/TDS sudah terpasang.
// ============================================================================
void generateRandomPHTDS() {
  sensorData.phValue     = randomFloat(5.5, 7.5);   // rentang pH umum hidroponik
  sensorData.tdsValuePpm = randomFloat(400, 1200);  // rentang TDS ppm
}

// ============================================================================
// FUNGSI: readWaterTemp()
// Membaca suhu air dari sensor DS18B20 (data asli).
// ============================================================================
void readWaterTemp() {
  waterTempSensor.requestTemperatures();
  float tempC = waterTempSensor.getTempCByIndex(0);
  if (tempC != DEVICE_DISCONNECTED_C) {
    sensorData.waterTempC = tempC;
  }
  // jika disconnect, nilai terakhir yang valid dipertahankan
}

// ============================================================================
// FUNGSI: readDHT()
// Membaca suhu & kelembapan udara dari sensor DHT22 (data asli).
// ============================================================================
void readDHT() {
  float t = dhtSensor.readTemperature();
  float h = dhtSensor.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    sensorData.airTempC = t;
    sensorData.airHumidityPct = h;
  }
  // jika gagal baca, nilai terakhir yang valid dipertahankan
}

// ============================================================================
// FUNGSI: randomFloat()
// Helper menghasilkan angka acak float dalam rentang tertentu.
// ============================================================================
float randomFloat(float minValue, float maxValue) {
  float scale = random(0, 10000) / 10000.0;
  return minValue + scale * (maxValue - minValue);
}

// ============================================================================
// FUNGSI: publishSensorData()
// Membentuk JSON dari data sensor dan mengirimkannya ke broker MQTT.
// ============================================================================
void publishSensorData() {
  JsonDocument jsonDoc;

  jsonDoc["serial_number"] = deviceSerialNumber;
  jsonDoc["ph"]            = round(sensorData.phValue * 100) / 100.0;
  jsonDoc["tds_ppm"]       = round(sensorData.tdsValuePpm);
  jsonDoc["water_temp_c"]  = round(sensorData.waterTempC * 10) / 10.0;
  jsonDoc["air_temp_c"]    = round(sensorData.airTempC * 10) / 10.0;
  jsonDoc["air_humidity"]  = round(sensorData.airHumidityPct * 10) / 10.0;

  char jsonBuffer[256];
  size_t jsonLength = serializeJson(jsonDoc, jsonBuffer);

  if (mqttClient.connected()) {
    mqttClient.publish(mqttTopic, jsonBuffer, jsonLength);
  }

  Serial.print(F("Publish -> "));
  Serial.println(jsonBuffer);
}

// ============================================================================
// FUNGSI: displayLCD()
// Tampilkan satu parameter per halaman, bergantian tiap 3 detik.
// page: 0=pH, 1=TDS, 2=Suhu Air, 3=Suhu Udara, 4=Kelembapan Udara
// ============================================================================
void displayLCD(uint8_t page) {
  lcd.clear();

  switch (page) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print(F("pH Larutan"));
      lcd.setCursor(0, 1);
      lcd.print(sensorData.phValue, 2);
      break;

    case 1:
      lcd.setCursor(0, 0);
      lcd.print(F("TDS"));
      lcd.setCursor(0, 1);
      lcd.print(sensorData.tdsValuePpm, 0);
      lcd.print(F(" ppm"));
      break;

    case 2:
      lcd.setCursor(0, 0);
      lcd.print(F("Suhu Air"));
      lcd.setCursor(0, 1);
      lcd.print(sensorData.waterTempC, 1);
      lcd.print((char)223);
      lcd.print(F("C"));
      break;

    case 3:
      lcd.setCursor(0, 0);
      lcd.print(F("Suhu Udara"));
      lcd.setCursor(0, 1);
      lcd.print(sensorData.airTempC, 1);
      lcd.print((char)223);
      lcd.print(F("C"));
      break;

    case 4:
      lcd.setCursor(0, 0);
      lcd.print(F("Kelembapan"));
      lcd.setCursor(0, 1);
      lcd.print(sensorData.airHumidityPct, 0);
      lcd.print(F(" %"));
      break;
  }
}

// ============================================================================
// FUNGSI: checkButton()
// Membaca tombol Fx dengan debounce. Berfungsi menyalakan kembali LCD
// dan mereset timer idle. Bisa dikembangkan untuk fungsi tambahan lain.
// ============================================================================
void checkButton(unsigned long currentMillis) {
  bool currentButtonState = digitalRead(PIN_BUTTON_FX); // LOW saat ditekan (pull-up)

  if (currentButtonState == LOW && lastButtonState == HIGH &&
      (currentMillis - lastButtonPressMillis > BUTTON_DEBOUNCE_MS)) {

    lastButtonPressMillis = currentMillis;
    lastActivityMillis = currentMillis;

    Serial.println(F("[BUTTON] Tombol Fx ditekan"));

    if (!lcdIsOn) {
      turnOnLCD();
    }
    // TODO: tambahkan aksi lain di sini jika tombol Fx perlu fungsi khusus
  }

  lastButtonState = currentButtonState;
}

// ============================================================================
// FUNGSI: checkLcdIdleTimeout()
// Mematikan backlight LCD setelah idle 1 menit tanpa aktivitas tombol.
// ============================================================================
void checkLcdIdleTimeout(unsigned long currentMillis) {
  if (lcdIsOn && (currentMillis - lastActivityMillis >= LCD_IDLE_TIMEOUT_MS)) {
    turnOffLCD();
  }
}

// ============================================================================
// FUNGSI: turnOnLCD() / turnOffLCD()
// ============================================================================
void turnOnLCD() {
  lcd.backlight();
  lcd.display();
  lcdIsOn = true;
  Serial.println(F("[LCD] ON"));
}

void turnOffLCD() {
  lcd.noBacklight();
  lcd.noDisplay();
  lcdIsOn = false;
  Serial.println(F("[LCD] OFF (idle timeout)"));
}
