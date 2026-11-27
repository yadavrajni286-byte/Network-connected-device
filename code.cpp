#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ---------------- WIFI SETTINGS ----------------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ---------------- MQTT SETTINGS ----------------
const char* MQTT_BROKER = "broker.hivemq.com";
const int MQTT_PORT = 1883;

const char* MQTT_TOPIC =
  "esp32/dht22/esp32_01_a91c7e/telemetry";

// ---------------- SENSOR SETTINGS ----------------
#define DHTPIN 4
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

// ---------------- NETWORK OBJECTS ----------------
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

String mqttClientId;

// ---------------- TIMERS ----------------
unsigned long lastReading = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long wifiAttemptStart = 0;
unsigned long lastMQTTAttempt = 0;
unsigned long lastBufferSend = 0;
unsigned long lastDiagnostic = 0;

const unsigned long SENSOR_INTERVAL = 10000;
const unsigned long WIFI_RETRY_INTERVAL = 5000;
const unsigned long WIFI_TIMEOUT = 15000;
const unsigned long MQTT_RETRY_INTERVAL = 5000;
const unsigned long BUFFER_SEND_INTERVAL = 1000;

// ---------------- WIFI STATE ----------------
bool wifiStarted = false;
bool wifiConnecting = false;
bool wasConnected = false;

// ---------------- WIFI DISCONNECT TEST ----------------
const bool ENABLE_WIFI_DISCONNECT_TEST = true;

unsigned long testStartTime = 0;
bool wifiTestTimerStarted = false;
bool wifiTestTriggered = false;

const unsigned long WIFI_DISCONNECT_TEST_TIME = 20000;

// ---------------- OFFLINE BUFFER ----------------
String buffer[20];
int bufferCount = 0;

// ------------------------------------------------
// CONNECT / RECONNECT WIFI
// ------------------------------------------------
void connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) {

    if (!wasConnected) {

      Serial.println();
      Serial.println("WiFi Connected!");

      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());

      Serial.println("==============================");

      wasConnected = true;
    }

    wifiConnecting = false;

    // Start the one-time test timer
    if (!wifiTestTimerStarted) {

      testStartTime = millis();
      wifiTestTimerStarted = true;

      Serial.println("WiFi test timer started.");
    }

    return;
  }

  // Detect WiFi disconnection
  if (wasConnected) {

    Serial.println();
    Serial.println("WiFi disconnected!");

    wasConnected = false;
    wifiConnecting = false;

    lastWiFiAttempt = millis();
  }

  // Handle connection timeout
  if (wifiConnecting &&
      millis() - wifiAttemptStart >= WIFI_TIMEOUT) {

    Serial.println("WiFi connection timeout!");

    WiFi.disconnect(false, false);

    wifiConnecting = false;
    lastWiFiAttempt = millis();
  }

  // Start or retry WiFi connection
  if (!wifiConnecting &&
      (!wifiStarted ||
       millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL)) {

    Serial.println("Connecting / Retrying WiFi...");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

    wifiStarted = true;
    wifiConnecting = true;

    wifiAttemptStart = millis();
    lastWiFiAttempt = millis();
  }
}

// ------------------------------------------------
// CONNECT / RECONNECT MQTT
// ------------------------------------------------
void connectMQTT() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  if (millis() - lastMQTTAttempt < MQTT_RETRY_INTERVAL) {
    return;
  }

  lastMQTTAttempt = millis();

  Serial.println();
  Serial.print("Connecting to MQTT broker...");

  if (mqttClient.connect(mqttClientId.c_str())) {

    Serial.println(" MQTT Connected!");

    Serial.print("Subscribed topic will be: ");
    Serial.println(MQTT_TOPIC);

  } else {

    Serial.print(" MQTT connection failed. State: ");
    Serial.println(mqttClient.state());
  }
}

// ------------------------------------------------
// SAVE SENSOR DATA TO BUFFER
// ------------------------------------------------
void saveToBuffer(String jsonData) {

  if (bufferCount < 20) {

    buffer[bufferCount] = jsonData;
    bufferCount++;

    Serial.println("Data saved in buffer");

    Serial.print("Buffer records: ");
    Serial.println(bufferCount);

  } else {

    Serial.println("Buffer FULL! New record not saved.");
  }
}

// ------------------------------------------------
// SEND BUFFERED DATA THROUGH MQTT
// ------------------------------------------------
void flushBuffer() {

  if (!mqttClient.connected() || bufferCount == 0) {
    return;
  }

  Serial.println();
  Serial.println("Publishing buffered MQTT data...");

  // Publish oldest record first.
  // Retain the latest successfully published message.
  bool success = mqttClient.publish(
    MQTT_TOPIC,
    buffer[0].c_str(),
    true
  );

  if (success) {

    Serial.println("MQTT publish accepted!");

    // Remove oldest record
    for (int i = 0; i < bufferCount - 1; i++) {
      buffer[i] = buffer[i + 1];
    }

    bufferCount--;

    Serial.println("Buffered data sent!");
    Serial.print("Remaining buffer records: ");
    Serial.println(bufferCount);

  } else {

    Serial.println("MQTT publish failed.");
    Serial.println("Keeping data in buffer.");
  }
}

// ------------------------------------------------
// READ DHT22 SENSOR
// ------------------------------------------------
void readSensor() {

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {

    Serial.println("DHT22 reading failed!");
    return;
  }

  char jsonData[160];

  snprintf(
    jsonData,
    sizeof(jsonData),
    "{\"device_id\":\"ESP32_01\","
    "\"temperature\":%.2f,"
    "\"humidity\":%.2f}",
    temperature,
    humidity
  );

  Serial.println();
  Serial.println("Sensor Data:");
  Serial.println(jsonData);

  saveToBuffer(String(jsonData));
}

// ------------------------------------------------
// SETUP
// ------------------------------------------------
void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32 MQTT Dashboard Project");
  Serial.println("================================");

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(3);
  mqttClient.setBufferSize(256);

  // Generate a unique MQTT client ID for this ESP32
  mqttClientId = "ESP32_01-" +
    String((uint32_t)ESP.getEfuseMac(), HEX);

  connectWiFi();
}

// ------------------------------------------------
// MAIN LOOP
// ------------------------------------------------
void loop() {

  // 1. Maintain WiFi connection
  connectWiFi();

  // 2. Run the automatic WiFi disconnect test once
  if (ENABLE_WIFI_DISCONNECT_TEST &&
      wifiTestTimerStarted &&
      !wifiTestTriggered &&
      millis() - testStartTime >= WIFI_DISCONNECT_TEST_TIME) {

    wifiTestTriggered = true;

    Serial.println();
    Serial.println("*** SIMULATING WIFI DISCONNECTION ***");
    Serial.println("ESP32 disconnecting from WiFi...");

    WiFi.disconnect(false, false);
  }

  // 3. Maintain MQTT connection
  connectMQTT();

  // 4. Process MQTT network traffic
  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // 5. Read DHT22 every 10 seconds
  if (millis() - lastReading >= SENSOR_INTERVAL) {

    lastReading = millis();

    readSensor();
  }

  // 6. Send buffered data when MQTT is connected
  if (mqttClient.connected() &&
      bufferCount > 0 &&
      millis() - lastBufferSend >= BUFFER_SEND_INTERVAL) {

    lastBufferSend = millis();

    flushBuffer();
  }

  // 7. Print diagnostics every 5 seconds
  if (millis() - lastDiagnostic >= 5000) {

    lastDiagnostic = millis();

    Serial.print("WiFi Status: ");
    Serial.println(WiFi.status());

    Serial.print("MQTT Status: ");
    Serial.println(mqttClient.connected() ? "Connected" : "Disconnected");

    Serial.print("Buffer Count: ");
    Serial.println(bufferCount);
  }

  delay(10);
}