/*
  Smart Classroom Occupancy System (with OLED)
  ESP32 + 2x IR sensors (directional people counting) + DHT22 + fan + OLED + Thinger.io

  Wiring:
    IR sensor 1 (outer/corridor)  OUT  -> GPIO14
    IR sensor 2 (inner/room)      OUT  -> GPIO25
    DHT22                         DATA -> GPIO4  (10k pull-up to VCC)
    Fan control (via transistor)  ->     GPIO33
    OLED (SSD1306, I2C 128x64)    VCC  -> 3.3V
                                   GND  -> GND
                                   SDA  -> GPIO21
                                   SCL  -> GPIO22

  IR1 then IR2 = person entered. IR2 then IR1 = person left.
  Most IR obstacle modules are ACTIVE LOW (output goes LOW when beam is broken).
  Set BEAM_BROKEN to HIGH below if yours behaves the opposite way.

  OLED behavior:
    - Normal state: dashboard showing Occupancy / Temp / Humidity / Fan status.
    - On ENTRY, EXIT, FAN ON, or FAN OFF: screen instantly switches to a big
      banner announcing the event, then automatically reverts back to the
      dashboard after EVENT_MSG_DURATION ms. This is done without any
      blocking delay() so sensor polling never stalls.

  Libraries required (Arduino Library Manager):
    - Adafruit SSD1306
    - Adafruit GFX Library
    - (Adafruit BusIO installs automatically as a dependency)
*/

#include <ThingerESP32.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Thinger.io credentials ----------
#define USERNAME "_________"
#define DEVICE_ID "_________"
#define DEVICE_CREDENTIAL "_______"

// ---------- WiFi credentials ----------
#define SSID "_______"
#define SSID_PASSWORD "___________"

// ---------- Pins ----------
#define IR1_PIN 14
#define IR2_PIN 25
#define DHT_PIN 4
#define FAN_PIN 33

// ---------- OLED ----------
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C   // change to 0x3D if your module needs it
#define OLED_RESET -1    // no dedicated reset pin

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);
ThingerESP32 thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL);

const int BEAM_BROKEN = LOW;
const float FAN_ON_TEMP = 24.0;

int occupancy = 0;
int lastSensor = 0;   // 0 = none, 1 = IR1 broke first, 2 = IR2 broke first

bool ir1Prev = false;
bool ir2Prev = false;

float temperature = 0;
float humidity = 0;
bool fanState = false;

unsigned long lastDHTRead = 0;
const unsigned long DHT_INTERVAL = 2500;

// ---------- OLED event/dashboard state ----------
bool eventActive = false;
unsigned long eventMsgUntil = 0;
const unsigned long EVENT_MSG_DURATION = 1500; // ms an event banner stays on screen

unsigned long lastDashboardDraw = 0;
const unsigned long DASHBOARD_REFRESH = 1000; // ms between idle dashboard redraws

// ---------- OLED helper functions ----------

// Draws the normal idle dashboard (occupancy/temp/humidity/fan)
void drawDashboard() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Classroom Monitor");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("Occ: ");
  display.println(occupancy);

  display.setTextSize(1);
  display.setCursor(0, 38);
  display.print("Temp: ");
  display.print(temperature, 1);
  display.println(" C");

  display.setCursor(0, 48);
  display.print("Hum:  ");
  display.print(humidity, 1);
  display.println(" %");

  display.setCursor(0, 58);
  display.print("Fan: ");
  display.print(fanState ? "ON" : "OFF");

  display.display();
}

// Instantly shows a big event banner (ENTRY/EXIT/FAN ON/FAN OFF) and arms
// the timer that reverts back to the dashboard.
void showEvent(const char* msg) {
  eventActive = true;
  eventMsgUntil = millis() + EVENT_MSG_DURATION;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 18);
  display.println(msg);

  display.setTextSize(1);
  display.setCursor(0, 46);
  display.print("Occupancy: ");
  display.println(occupancy);
  display.setCursor(0, 56);
  display.print("Fan: ");
  display.print(fanState ? "ON" : "OFF");

  display.display();
}

void setup() {
  Serial.begin(115200);

  pinMode(IR1_PIN, INPUT);
  pinMode(IR2_PIN, INPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);

  dht.begin();

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed - check wiring/address");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("Starting up...");
    display.display();
  }

  thing.add_wifi(SSID, SSID_PASSWORD);

  thing["occupancy"] >> [](pson &out) { out = occupancy; };
  thing["temperature"] >> [](pson &out) { out = temperature; };
  thing["humidity"] >> [](pson &out) { out = humidity; };
  thing["fan_status"] >> [](pson &out) { out = fanState; };

  thing["fan_manual"] << [](pson &in) {
    if (in.is_empty()) return;
    bool manualState = in;
    digitalWrite(FAN_PIN, manualState ? HIGH : LOW);
    if (manualState != fanState) {
      fanState = manualState;
      showEvent(fanState ? "FAN ON" : "FAN OFF");
    }
  };

  thing["reset_count"] << [](pson &in) {
    occupancy = 0;
    drawDashboard();
  };

  Serial.println("Smart classroom occupancy system starting...");
  drawDashboard();
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
      showEvent("EXIT");   // instant OLED update
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
      showEvent("ENTRY");  // instant OLED update
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
        showEvent("FAN ON");   // instant OLED update
      } else if (temperature <= FAN_ON_TEMP && fanState) {
        digitalWrite(FAN_PIN, LOW);
        fanState = false;
        showEvent("FAN OFF");  // instant OLED update
      }
    } else {
      Serial.println("DHT22 read failed");
    }
  }

  // Revert OLED from event banner back to dashboard once the timer expires,
  // and otherwise keep the idle dashboard refreshed (non-blocking).
  if (eventActive && now >= eventMsgUntil) {
    eventActive = false;
    drawDashboard();
    lastDashboardDraw = now;
  } else if (!eventActive && (now - lastDashboardDraw >= DASHBOARD_REFRESH)) {
    drawDashboard();
    lastDashboardDraw = now;
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
