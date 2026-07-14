#include <Arduino.h>
#include <ThingerESP32.h>
#include <DHT.h>

#define USERNAME "YOUR_THINGER_USERNAME"
#define DEVICE_ID "YOUR_DEVICE_ID"
#define DEVICE_CREDENTIAL "YOUR_DEVICE_CREDENTIAL"

#define SSID "YOUR_WIFI_SSID"
#define SSID_PASSWORD "YOUR_WIFI_PASSWORD"

#define IR1_PIN 14
#define IR2_PIN 25
#define DHT_PIN 4
#define FAN_PIN 33

#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);
ThingerESP32 thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL);

const int BEAM_BROKEN = LOW;
const float FAN_ON_TEMP = 29.0;

int occupancy = 0;
int lastSensor = 0;   // 0 = none, 1 = IR1 broke first, 2 = IR2 broke first

bool ir1Prev = false;
bool ir2Prev = false;

float temperature = 0;
float humidity = 0;
bool fanState = false;

unsigned long lastDHTRead = 0;
const unsigned long DHT_INTERVAL = 2500;

void setup() {
  Serial.begin(115200);

  pinMode(IR1_PIN, INPUT);
  pinMode(IR2_PIN, INPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);

  dht.begin();
  thing.add_wifi(SSID, SSID_PASSWORD);

  thing["occupancy"] >> [](pson &out) { out = occupancy; };
  thing["temperature"] >> [](pson &out) { out = temperature; };
  thing["humidity"] >> [](pson &out) { out = humidity; };
  thing["fan_status"] >> [](pson &out) { out = fanState; };

  thing["fan_manual"] << [](pson &in) {
    if (in.is_empty()) return;
    bool manualState = in;
    digitalWrite(FAN_PIN, manualState ? HIGH : LOW);
    fanState = manualState;
  };

  thing["reset_count"] << [](pson &in) { occupancy = 0; };

  Serial.println("Smart classroom occupancy system starting...");
}

void loop() {
  thing.handle();

  bool ir1 = (digitalRead(IR1_PIN) == BEAM_BROKEN);
  bool ir2 = (digitalRead(IR2_PIN) == BEAM_BROKEN);

  if (ir1 && !ir1Prev) {
    if (lastSensor == 2) {
      occupancy = max(0, occupancy - 1);
      Serial.print("EXIT -> occupancy: ");
      Serial.println(occupancy);
      lastSensor = 0;
    } else {
      lastSensor = 1;
    }
  }

  if (ir2 && !ir2Prev) {
    if (lastSensor == 1) {
      occupancy++;
      Serial.print("ENTRY -> occupancy: ");
      Serial.println(occupancy);
      lastSensor = 0;
    } else {
      lastSensor = 2;
    }
  }

  ir1Prev = ir1;
  ir2Prev = ir2;

  unsigned long now = millis();
  if (now - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity = h;
      if (temperature > FAN_ON_TEMP && !fanState) {
        digitalWrite(FAN_PIN, HIGH);
        fanState = true;
      } else if (temperature <= FAN_ON_TEMP && fanState) {
        digitalWrite(FAN_PIN, LOW);
        fanState = false;
      }
    } else {
      Serial.println("DHT22 read failed");
    }
  }

  Serial.print("IR1: ");
  Serial.print(ir1 ? "BLOCKED" : "clear  ");
  Serial.print("  IR2: ");
  Serial.print(ir2 ? "BLOCKED" : "clear  ");
  Serial.print("  occupancy: ");
  Serial.print(occupancy);
  Serial.print("  temp: ");
  Serial.print(temperature);
  Serial.print("C  humidity: ");
  Serial.print(humidity);
  Serial.print("%  fan: ");
  Serial.println(fanState ? "ON" : "OFF");

  delay(200);
}