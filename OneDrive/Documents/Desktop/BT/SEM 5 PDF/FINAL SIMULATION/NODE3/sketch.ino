/*
 * Smart Kitchen Garden Monitoring - SENSOR NODE 3 (Plant Pod 3)
 */
#define WOKWI_SIMULATION 1

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

#if WOKWI_SIMULATION
  #include <WiFi.h>
  #include <PubSubClient.h>
#else
  #include <SPI.h>
  #include <LoRa.h>
#endif

// ================= NODE CONFIGURATION =================
const int NODE_ID = 3;
const char* POD_NAME = "Pod 3";
const char* MQTT_TOPIC = "kitchen_garden_lora/node3";

// Pin Definitions
#define DHTPIN 4
#define DHTTYPE DHT22
#define SOIL_PIN 34
#define TDS_PIN 35
#define TX_LED_PIN 2

#if !WOKWI_SIMULATION
  #define LORA_SS 5
  #define LORA_RST 14
  #define LORA_DIO0 26
#endif

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

const int MOISTURE_THRESHOLD = 40;  // Below 40% = Dry soil
const int TDS_THRESHOLD = 700;       // Above 700 ppm = High TDS alert

#if WOKWI_SIMULATION
  const char* WIFI_SSID = "Wokwi-GUEST";
  const char* WIFI_PASSWORD = "";
  const char* MQTT_BROKER = "broker.hivemq.com";
  const int MQTT_PORT = 1883;

  WiFiClient espClient;
  PubSubClient mqttClient(espClient);

  void setupWiFi() {
    delay(10);
    Serial.println();
    Serial.print("[WiFi] Connecting to ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 25) {
      delay(300);
      Serial.print(".");
      retries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WiFi] Connected successfully!");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\n[WiFi] Connection pending, will retry in background...");
    }
  }

  void reconnectMQTT() {
    while (!mqttClient.connected()) {
      Serial.print("[MQTT] Connecting to broker.hivemq.com...");
      String clientId = "KitchenGardenNode-" + String(NODE_ID) + "-" + String(random(0xffff), HEX);
      if (mqttClient.connect(clientId.c_str())) {
        Serial.println(" Connected successfully!");
      } else {
        Serial.print(" Failed (rc=");
        Serial.print(mqttClient.state());
        Serial.println("), retrying in 2 seconds...");
        delay(2000);
      }
    }
  }
#endif

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(TX_LED_PIN, OUTPUT);
  digitalWrite(TX_LED_PIN, LOW);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Kitchen Garden");
  lcd.setCursor(0, 1);
  lcd.print(String(POD_NAME) + " Init");

  dht.begin();

  Serial.println("\n==========================================");
  Serial.printf(" SMART KITCHEN GARDEN - NODE %d [%s]\n", NODE_ID, POD_NAME);
  Serial.println("==========================================");

#if WOKWI_SIMULATION
  Serial.println("[MODE] Simulation via MQTT Virtual LoRa");
  setupWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
#else
  Serial.println("[MODE] Physical SX1278 LoRa Hardware");
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (LoRa.begin(433E6)) {
    Serial.println("[LoRa] SX1278 transceiver ready at 433 MHz");
  } else {
    Serial.println("[LoRa] Transceiver failed to start!");
  }
#endif

  delay(1500);
  lcd.clear();
}

unsigned long lastTransmitTime = 0;

void loop() {
#if WOKWI_SIMULATION
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();
#endif

  if (millis() - lastTransmitTime >= 2500) {
    lastTransmitTime = millis();

    int soilRaw = analogRead(SOIL_PIN);
    int tdsRaw = analogRead(TDS_PIN);
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (isnan(temp) || isnan(hum)) {
      temp = 25.0;
      hum = 60.0;
    }

    int moisturePercent = map(soilRaw, 0, 4095, 0, 100);
    int tdsPpm = map(tdsRaw, 0, 4095, 0, 1200);

    bool soilDry = (moisturePercent < MOISTURE_THRESHOLD);
    bool tdsAbnormal = (tdsPpm > TDS_THRESHOLD);

    Serial.println("\n-------------------------------------------");
    Serial.printf(" [NODE %d - %s]\n", NODE_ID, POD_NAME);
    Serial.printf(" Moisture    : %d %% (%s)\n", moisturePercent, soilDry ? "DRY -> PUMP ON" : "OPTIMAL");
    Serial.printf(" TDS Level   : %d ppm (%s)\n", tdsPpm, tdsAbnormal ? "ALERT: HIGH!" : "NORMAL");
    Serial.printf(" Temperature : %.1f C | Humidity: %.1f %%\n", temp, hum);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.printf("N%d M:%d%% TDS:%d", NODE_ID, moisturePercent, tdsPpm);
    lcd.setCursor(0, 1);
    if (tdsAbnormal) {
      lcd.print("! TDS HIGH ALERT");
    } else if (soilDry) {
      lcd.print("! SOIL DRY (PUMP)");
    } else {
      lcd.printf("T:%.1fC H:%.0f%% OK", temp, hum);
    }

    String payload = "NODE" + String(NODE_ID) + "," + 
                     String(moisturePercent) + "," + 
                     String(tdsPpm) + "," + 
                     String(temp, 1) + "," + 
                     String(hum, 1) + "," + 
                     (tdsAbnormal ? "ALERT" : (soilDry ? "DRY" : "OK"));

    digitalWrite(TX_LED_PIN, HIGH);

#if WOKWI_SIMULATION
    Serial.print(" [VIRTUAL LORA TX -> MQTT] Packet: ");
    Serial.print(payload);
    boolean published = mqttClient.publish(MQTT_TOPIC, payload.c_str());
    if (published) {
      Serial.println(" -> [SUCCESS DELIVERED]");
    } else {
      Serial.println(" -> [FAILED TO SEND]");
    }
    mqttClient.loop();
#else
    Serial.print(" [HARDWARE LORA TX] Packet: ");
    Serial.println(payload);
    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket();
#endif

    delay(100);
    digitalWrite(TX_LED_PIN, LOW);
  }

  delay(20);
}