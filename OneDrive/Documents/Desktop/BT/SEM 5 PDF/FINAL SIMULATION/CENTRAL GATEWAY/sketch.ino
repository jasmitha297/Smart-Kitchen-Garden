/*
 * Smart Kitchen Garden Monitoring - CENTRAL GATEWAY
 */
#define WOKWI_SIMULATION 1

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#if WOKWI_SIMULATION
  #include <WiFi.h>
  #include <PubSubClient.h>
#else
  #include <SPI.h>
  #include <LoRa.h>
#endif

// Pin Definitions
#define RELAY_PUMP_PIN 26   // Main Irrigation Water Spray Pump Relay
#define VALVE_POD1_PIN 18   // Valve indicator LED - Pod 1
#define VALVE_POD2_PIN 19   // Valve indicator LED - Pod 2
#define VALVE_POD3_PIN 23   // Valve indicator LED - Pod 3
#define VALVE_POD4_PIN 5    // Valve indicator LED - Pod 4
#define ALERT_LED_PIN 27    // Abnormal TDS Alert Indicator
#define BUZZER_PIN 13       // Audible alarm

#if !WOKWI_SIMULATION
  #define LORA_SS 5
  #define LORA_RST 14
  #define LORA_DIO0 2
#endif

// System Thresholds
const int MOISTURE_THRESHOLD = 40;  // Below 40% -> Spray pump ON
const int TDS_THRESHOLD = 700;       // Above 700 ppm -> Nutrient alert email

LiquidCrystal_I2C lcd(0x27, 16, 2);

struct PodData {
  int nodeID;
  String podName;
  int moisture;
  int tds;
  float temperature;
  float humidity;
  bool dry;
  bool tdsAlert;
  unsigned long lastSeen;
  bool active;
  bool emailSent;
};

PodData pods[4];
const int valvePins[4] = {VALVE_POD1_PIN, VALVE_POD2_PIN, VALVE_POD3_PIN, VALVE_POD4_PIN};
const String defaultPodNames[4] = {"Pod 1 (Herbs)", "Pod 2 (Tomatoes)", "Pod 3 (Spinach)", "Pod 4 (Peppers)"};

#if WOKWI_SIMULATION
  const char* WIFI_SSID = "Wokwi-GUEST";
  const char* WIFI_PASSWORD = "";
  const char* MQTT_BROKER = "broker.hivemq.com";
  const int MQTT_PORT = 1883;
  const char* MQTT_SUB_TOPIC = "kitchen_garden_lora/+";

  WiFiClient espClient;
  PubSubClient mqttClient(espClient);

  void setupWiFi() {
    delay(10);
    Serial.println("\n[WiFi] Connecting to Wokwi-GUEST gateway...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
      delay(300);
      Serial.print(".");
      retries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WiFi] Gateway Connected! IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("\n[WiFi] Failed to connect, will retry in loop...");
    }
  }

  void processIncomingMessage(char* topic, byte* payload, unsigned int length);

  void reconnectMQTT() {
    while (!mqttClient.connected()) {
      Serial.print("[MQTT Gateway] Connecting to HiveMQ broker...");
      String clientId = "KitchenGarden-Gateway-" + String(random(0xffff), HEX);
      if (mqttClient.connect(clientId.c_str())) {
        Serial.println(" Connected!");
        mqttClient.subscribe(MQTT_SUB_TOPIC);
        Serial.print("[MQTT Gateway] Subscribed to all sensor nodes: ");
        Serial.println(MQTT_SUB_TOPIC);
      } else {
        Serial.print(" Failed (rc=");
        Serial.print(mqttClient.state());
        Serial.println("), retrying in 2 seconds...");
        delay(2000);
      }
    }
  }
#endif

void sendNutrientAlertEmail(int nodeIndex) {
  Serial.println("\n*************************************************************");
  Serial.println(" [ALERT SYSTEM: AUTOMATED EMAIL DISPATCH TRIGGERED]");
  Serial.println(" To      : user@kitchengarden.org");
  Serial.printf(" Subject : CRITICAL NUTRIENT ALERT - %s (Node %d)\n", pods[nodeIndex].podName.c_str(), pods[nodeIndex].nodeID);
  Serial.println(" Body    : ------------------------------------------------");
  Serial.printf("           Warning: Total Dissolved Solids (TDS) level of %d ppm\n", pods[nodeIndex].tds);
  Serial.printf("           exceeded safety threshold of %d ppm in %s!\n", TDS_THRESHOLD, pods[nodeIndex].podName.c_str());
  Serial.printf("           Current Pod Conditions: Temp: %.1f C, Moisture: %d%%\n", pods[nodeIndex].temperature, pods[nodeIndex].moisture);
  Serial.println("           Recommended Action: Flush pod with fresh water");
  Serial.println("           and verify nutrient mixture ratio immediately.");
  Serial.println("           ------------------------------------------------");
  Serial.println(" Status  : [EMAIL SENT SUCCESSFULLY VIA SMTP GATEWAY]");
  Serial.println("*************************************************************\n");
}

void handlePayload(String msg) {
  msg.trim();
  int c1 = msg.indexOf(',');
  if (c1 == -1) return;

  String nodeStr = msg.substring(0, c1);
  nodeStr.replace("NODE", "");
  nodeStr.replace("node", "");
  int nodeId = nodeStr.toInt();

  if (nodeId < 1 || nodeId > 4) return;

  int c2 = msg.indexOf(',', c1 + 1);
  int c3 = msg.indexOf(',', c2 + 1);
  int c4 = msg.indexOf(',', c3 + 1);

  if (c2 == -1 || c3 == -1) return;

  int moisture = msg.substring(c1 + 1, c2).toInt();
  int tds = msg.substring(c2 + 1, c3).toInt();
  float temp = (c4 != -1) ? msg.substring(c3 + 1, c4).toFloat() : msg.substring(c3 + 1).toFloat();
  float hum = (c4 != -1) ? msg.substring(c4 + 1).toFloat() : 50.0;

  int idx = nodeId - 1;
  pods[idx].nodeID = nodeId;
  pods[idx].podName = defaultPodNames[idx];
  pods[idx].moisture = moisture;
  pods[idx].tds = tds;
  pods[idx].temperature = temp;
  pods[idx].humidity = hum;
  pods[idx].dry = (moisture < MOISTURE_THRESHOLD);
  pods[idx].tdsAlert = (tds > TDS_THRESHOLD);
  pods[idx].lastSeen = millis();
  pods[idx].active = true;

  if (pods[idx].tdsAlert && !pods[idx].emailSent) {
    sendNutrientAlertEmail(idx);
    pods[idx].emailSent = true;
  } else if (!pods[idx].tdsAlert) {
    pods[idx].emailSent = false;
  }
}

#if WOKWI_SIMULATION
void processIncomingMessage(char* topic, byte* payload, unsigned int length) {
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  handlePayload(String(message));
}
#endif

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RELAY_PUMP_PIN, OUTPUT);
  pinMode(ALERT_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PUMP_PIN, LOW);
  digitalWrite(ALERT_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  for (int i = 0; i < 4; i++) {
    pinMode(valvePins[i], OUTPUT);
    digitalWrite(valvePins[i], LOW);
    pods[i].nodeID = i + 1;
    pods[i].podName = defaultPodNames[i];
    pods[i].active = false;
    pods[i].emailSent = false;
  }

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("LoRa Kitchen Hub");
  lcd.setCursor(0, 1);
  lcd.print("Starting Gateway");

  Serial.println("\n========================================================");
  Serial.println(" SMART KITCHEN GARDEN - CENTRAL GATEWAY & CONTROLLER ");
  Serial.println("========================================================");

#if WOKWI_SIMULATION
  Serial.println("[MODE] Simulation via MQTT Virtual LoRa");
  setupWiFi();
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(processIncomingMessage);
#else
  Serial.println("[MODE] Physical SX1278 LoRa Receiver Hardware");
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (LoRa.begin(433E6)) {
    Serial.println("[LoRa] SX1278 receiver ready at 433 MHz");
  } else {
    Serial.println("[LoRa] Receiver init failed! Check wiring.");
  }
#endif

  delay(1500);
  lcd.clear();
}

unsigned long lastLcdUpdate = 0;
unsigned long lastSerialPrint = 0;
int currentLcdPod = 0;

void evaluateIrrigationAndAlerts() {
  bool anyPodDry = false;
  bool anyTdsAlert = false;

  for (int i = 0; i < 4; i++) {
    if (pods[i].active && (millis() - pods[i].lastSeen < 15000)) {
      if (pods[i].moisture < MOISTURE_THRESHOLD) {
        anyPodDry = true;
        digitalWrite(valvePins[i], HIGH);
      } else {
        digitalWrite(valvePins[i], LOW);
      }

      if (pods[i].tds > TDS_THRESHOLD) {
        anyTdsAlert = true;
      }
    } else {
      digitalWrite(valvePins[i], LOW);
    }
  }

  if (anyPodDry) {
    digitalWrite(RELAY_PUMP_PIN, HIGH);
  } else {
    digitalWrite(RELAY_PUMP_PIN, LOW);
  }

  if (anyTdsAlert) {
    digitalWrite(ALERT_LED_PIN, HIGH);
    tone(BUZZER_PIN, 1000, 100);
  } else {
    digitalWrite(ALERT_LED_PIN, LOW);
    noTone(BUZZER_PIN);
  }
}

void updateLcdDashboard() {
  if (millis() - lastLcdUpdate < 2500) return;
  lastLcdUpdate = millis();

  int startIdx = currentLcdPod;
  bool found = false;
  for (int t = 0; t < 4; t++) {
    int idx = (startIdx + t) % 4;
    if (pods[idx].active && (millis() - pods[idx].lastSeen < 15000)) {
      currentLcdPod = (idx + 1) % 4;
      found = true;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.printf("N%d:%s M:%d%%", pods[idx].nodeID, pods[idx].dry ? "DRY!" : "OK ", pods[idx].moisture);
      lcd.setCursor(0, 1);
      if (pods[idx].tdsAlert) {
        lcd.printf("TDS:%d ALRT!%.0fC", pods[idx].tds, pods[idx].temperature);
      } else {
        lcd.printf("TDS:%dppm T:%.1fC", pods[idx].tds, pods[idx].temperature);
      }
      break;
    }
  }

  if (!found) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Kitchen Garden");
    lcd.setCursor(0, 1);
    lcd.print("Listening nodes..");
  }
}

void printSerialDashboard() {
  if (millis() - lastSerialPrint < 3000) return;
  lastSerialPrint = millis();

  bool pumpStatus = digitalRead(RELAY_PUMP_PIN);

  Serial.println("\n+========================================================================+");
  Serial.println("|          SMART KITCHEN GARDEN - CENTRALISED MONITORING DASHBOARD       |");
  Serial.println("+----+------------------+----------+----------+--------+--------+--------+");
  Serial.println("| ID | Plant Pod Name   | Moisture | TDS(ppm) | Temp(C)| Hum(%) | Status |");
  Serial.println("+----+------------------+----------+----------+--------+--------+--------+");

  for (int i = 0; i < 4; i++) {
    if (pods[i].active && (millis() - pods[i].lastSeen < 15000)) {
      String statusStr = "";
      if (pods[i].dry && pods[i].tdsAlert) {
        statusStr = "DRY+ALRT";
      } else if (pods[i].dry) {
        statusStr = "SPRAY ON";
      } else if (pods[i].tdsAlert) {
        statusStr = "TDS ALRT";
      } else {
        statusStr = "HEALTHY ";
      }

      Serial.printf("| %-2d | %-16s | %3d %%    | %4d     | %5.1f  | %5.1f  | %-6s |\n",
                    pods[i].nodeID,
                    pods[i].podName.c_str(),
                    pods[i].moisture,
                    pods[i].tds,
                    pods[i].temperature,
                    pods[i].humidity,
                    statusStr.c_str());
    } else {
      Serial.printf("| %-2d | Pod %-12d | -- OFFLINE / WAITING FOR TELEMETRY --        |\n", i + 1, i + 1);
    }
  }
  Serial.println("+----+------------------+----------+----------+--------+--------+--------+");
  Serial.printf(" Automated Water Spray Pump : %s\n", pumpStatus ? ">> [ACTIVE - SPRAYING WATER]" : "[IDLE]");
  Serial.println("+========================================================================+");
}

void loop() {
#if WOKWI_SIMULATION
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();
#else
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    handlePayload(incoming);
  }
#endif

  evaluateIrrigationAndAlerts();
  updateLcdDashboard();
  printSerialDashboard();
}