#include <WiFi.h>
#include <esp_now.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define SOIL_PIN 34
#define RELAY_PIN 26

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Structure match for transmission across nodes
typedef struct struct_message {
    int nodeID;
    float temperature;
    int soilMoisture;
    bool relayState;
} struct_message;

struct_message myData;
struct_message incomingData;

// Global array storage for the Gateway to cache node payloads
struct_message nodeDataStore[5]; 

// Master MAC Address for peer setup
uint8_t masterAddress[] = {0x24, 0x0A, 0xC4, 0x12, 0x34, 0x56};

int assignedNodeID = 2; // Default fallback to Slave Node 2
bool isMaster = false;
int displayIndex = 0;
unsigned long lastDisplaySwitch = 0;

// Data Receive Callback for Master Node
void OnDataRecv(const esp_now_recv_info_t *recvInfo, const uint8_t *incomingByte, int len) {
  if (!isMaster) return;
  
  memcpy(&incomingData, incomingByte, sizeof(incomingData));
  
  if (incomingData.nodeID >= 1 && incomingData.nodeID <= 5) {
    nodeDataStore[incomingData.nodeID - 1] = incomingData;
    
    Serial.print("Gateway Received from Node ");
    Serial.print(incomingData.nodeID);
    Serial.print(": Temp="); Serial.print(incomingData.temperature);
    Serial.print("C, Soil="); Serial.print(incomingData.soilMoisture);
    Serial.print("%, Pump="); Serial.println(incomingData.relayState ? "ON" : "OFF");
  }
}

// Data Sent Status Callback for Slave Nodes
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Node ");
  Serial.print(assignedNodeID);
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? " Delivery Success" : " Delivery Fail");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  dht.begin();
  lcd.init();
  lcd.backlight();

  // Initialize WiFi station mode
  WiFi.mode(WIFI_MODE_STA);
  String macStr = WiFi.macAddress();
  Serial.print("System Booting. MAC Address: ");
  Serial.println(macStr);

  // Dynamic role assignment based on Wokwi diagram positions
  if (macStr == "24:0A:C4:12:34:56" || macStr == "24:0a:c4:12:34:56") {
    isMaster = true;
    assignedNodeID = 1;
    Serial.println("Role Confirmed: MASTER GATEWAY (Node 1)");
  } else if (macStr == "24:0A:C4:12:34:57" || macStr == "24:0a:c4:12:34:57") {
    assignedNodeID = 2;
    Serial.println("Role Confirmed: SLAVE NODE 2");
  } else if (macStr == "24:0A:C4:12:34:58" || macStr == "24:0a:c4:12:34:58") {
    assignedNodeID = 3;
    Serial.println("Role Confirmed: SLAVE NODE 3");
  } else if (macStr == "24:0A:C4:12:34:59" || macStr == "24:0a:c4:12:34:59") {
    assignedNodeID = 4;
    Serial.println("Role Confirmed: SLAVE NODE 4");
  } else {
    assignedNodeID = 5;
    Serial.println("Role Confirmed: SLAVE NODE 5");
  }

  // Initialize ESP-NOW Protocol
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  if (isMaster) {
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    // Clear out data storage with initialization parameters
    for (int i = 0; i < 5; i++) {
      nodeDataStore[i].nodeID = i + 1;
      nodeDataStore[i].temperature = 0.0;
      nodeDataStore[i].soilMoisture = 0;
      nodeDataStore[i].relayState = false;
    }
  } else {
    esp_now_register_send_cb(OnDataSent);
    
    // Bind Master Node as communication target
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, masterAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add Gateway peer configuration");
      return;
    }
  }
  
  lcd.clear();
}

void loop() {
  // Read local environmental factors
  float temp = dht.readTemperature();
  int rawAnalog = analogRead(SOIL_PIN);
  int soil = map(rawAnalog, 0, 4095, 0, 100);

  if (isnan(temp)) temp = 0.0;

  // Local automation loop for irrigation
  bool pumpState = false;
  if (soil < 30) {
    digitalWrite(RELAY_PIN, HIGH);
    pumpState = true;
  } else {
    digitalWrite(RELAY_PIN, LOW);
    pumpState = false;
  }

  // Pack telemetry parameters
  myData.nodeID = assignedNodeID;
  myData.temperature = temp;
  myData.soilMoisture = soil;
  myData.relayState = pumpState;

  if (!isMaster) {
    // Slaves broadcast telemetry frames to the Master Node
    esp_now_send(masterAddress, (uint8_t *)&myData, sizeof(myData));
    
    // Update local node panel display
    lcd.setCursor(0, 0);
    lcd.print("Node "); lcd.print(assignedNodeID); lcd.print(" Status   ");
    lcd.setCursor(0, 1);
    lcd.print("T:"); lcd.print(temp, 1); lcd.print("C S:"); lcd.print(soil); lcd.print("%   ");
  } 
  else {
    // Gateway caches its local dataset inside slot 0
    nodeDataStore[0] = myData;
    
    // Master switches its display readouts every 2 seconds to loop through nodes 1–5
    if (millis() - lastDisplaySwitch > 2000) {
      lastDisplaySwitch = millis();
      
      lcd.setCursor(0, 0);
      lcd.print("Gateway Display ");
      lcd.setCursor(0, 1);
      
      int id = nodeDataStore[displayIndex].nodeID;
      float t = nodeDataStore[displayIndex].temperature;
      int s = nodeDataStore[displayIndex].soilMoisture;
      
      lcd.print("N"); lcd.print(id);
      lcd.print(" T:"); lcd.print(t, 1);
      lcd.print(" S:"); lcd.print(s); lcd.print("%    ");
      
      displayIndex++;
      if (displayIndex >= 5) displayIndex = 0;
    }
  }

  delay(1000); // Balance iteration updates across Wokwi instances
}
