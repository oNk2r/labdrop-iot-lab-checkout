#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ---------- Pins ----------
#define SS_PIN 5
#define RST_PIN 4
#define LED_GREEN 26
#define LED_RED 27
#define BUZZER 25

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* MQTT_HOST = "broker.hivemq.com";
const int MQTT_PORT = 1883;

// ROOT must NOT contain /#
const char* ROOT = "labdrop-jack-4821";

WiFiClient net;
PubSubClient mqtt(net);

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------- Equipment ----------
struct Equipment {
  const char* id;
  const char* name;
  byte uid[4];
  bool borrowed;
  const char* by;
};

Equipment items[] = {
  {"024", "ESP32 Kit #024",
   {0x11, 0x22, 0x33, 0x44}, false, nullptr},

  {"011", "Arduino Kit #011",
   {0xA1, 0xA2, 0xA3, 0xA4}, false, nullptr},

  {"007", "Sensor Kit #007",
   {0xB1, 0xB2, 0xB3, 0xB4}, false, nullptr},

  {"008", "Yellow Equipment #008",
   {0x55, 0x66, 0x77, 0x88}, false, nullptr},

  {"009", "Red Equipment #009",
   {0xAA, 0xBB, 0xCC, 0xDD}, false, nullptr},

  {"010", "NFC Equipment #010",
   {0x04, 0x11, 0x22, 0x33}, false, nullptr},

  {"012", "Key Fob Equipment #012",
   {0xC0, 0xFF, 0xEE, 0x99}, false, nullptr}
};

const int ITEM_COUNT = sizeof(items) / sizeof(items[0]);

// ---------- Students ----------
struct Student {
  const char* name;
  byte uid[4];
};

Student students[] = {
  {"Jack",  {0x01, 0x02, 0x03, 0x04}},
  {"Asha",  {0xC1, 0xC2, 0xA3, 0xA4}},
  {"Rahul", {0xD1, 0xD2, 0xD3, 0xD4}}
};

const int STUDENT_COUNT = sizeof(students) / sizeof(students[0]);

int activeStudent = -1;
unsigned long activeUntil = 0;

// ---------- Helpers ----------
void beep(int frequency, int duration) {
  tone(BUZZER, frequency, duration);
  delay(duration);
  noTone(BUZZER);
}

void setLeds(bool green) {
  digitalWrite(LED_GREEN, green ? HIGH : LOW);
  digitalWrite(LED_RED, green ? LOW : HIGH);
}

void show(const char* a, const char* b,
          const char* c, const char* d) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 0);
  oled.println(a);
  oled.setCursor(0, 16);
  oled.println(b);
  oled.setCursor(0, 32);
  oled.println(c);
  oled.setCursor(0, 48);
  oled.println(d);
  oled.display();
}

void showIdle() {
  int borrowedCount = 0;

  for (int i = 0; i < ITEM_COUNT; i++) {
    if (items[i].borrowed) borrowedCount++;
  }

  char line3[24];
  char line4[24];

  snprintf(line3, sizeof(line3), "AVAILABLE: %d",
           ITEM_COUNT - borrowedCount);
  snprintf(line4, sizeof(line4), "BORROWED : %d",
           borrowedCount);

  show("LABDROP",
       mqtt.connected() ? "SCAN TAG [online]" : "SCAN TAG [offline]",
       line3, line4);

  setLeds(borrowedCount == 0);
}

int findItem(byte* uid) {
  for (int i = 0; i < ITEM_COUNT; i++) {
    if (memcmp(uid, items[i].uid, 4) == 0) return i;
  }
  return -1;
}

int findStudent(byte* uid) {
  for (int i = 0; i < STUDENT_COUNT; i++) {
    if (memcmp(uid, students[i].uid, 4) == 0) return i;
  }
  return -1;
}

// ---------- MQTT ----------
String topic(const char* suffix) {
  return String(ROOT) + "/" + suffix;
}

String topic(const String& suffix) {
  return String(ROOT) + "/" + suffix;
}

void publishItem(int i, const char* event) {
  if (!mqtt.connected()) return;

  char buf[256];

  snprintf(
    buf, sizeof(buf),
    "{\"id\":\"%s\",\"name\":\"%s\",\"status\":\"%s\","
    "\"by\":\"%s\",\"ts\":%ld}",
    items[i].id,
    items[i].name,
    items[i].borrowed ? "borrowed" : "available",
    items[i].by ? items[i].by : "-",
    (long)time(nullptr)
  );

  String equipmentTopic = topic(String("equipment/") + items[i].id);

  // Retained state allows Node-RED to rebuild the 7-item table.
  mqtt.publish(equipmentTopic.c_str(), buf, true);

  // Events feed activity table, chart, and CSV log.
  if (event != nullptr) {
    mqtt.publish(topic("events").c_str(), buf, false);
  }
}

void mqttConnect() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;

  String clientId = "labdrop-" + String(esp_random(), HEX);
  Serial.print("MQTT connecting... ");

  if (mqtt.connect(clientId.c_str())) {
    Serial.println("OK");
    mqtt.publish(topic("status").c_str(), "online", true);

    for (int i = 0; i < ITEM_COUNT; i++) {
      publishItem(i, nullptr);
    }

    showIdle();
  } else {
    Serial.printf("failed, rc=%d\n", mqtt.state());
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  Wire.begin(21, 22);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED initialization failed");
    while (true) delay(1000);
  }

  SPI.begin(18, 19, 23, SS_PIN);
  rfid.PCD_Init();

  show("LABDROP", "Connecting Wi-Fi...", "", "");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);

  Serial.print("Wi-Fi");
  for (byte i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" FAILED (running offline)");
  }

  // IST timestamps.
  configTime(19800, 0, "pool.ntp.org");

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(60);
  mqtt.setBufferSize(512);
  mqttConnect();
  showIdle();
}

// ---------- RFID ----------
void finishCard() {
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ---------- Main Loop ----------
void loop() {
  static unsigned long lastTry = 0;

  if (!mqtt.connected() &&
      WiFi.status() == WL_CONNECTED &&
      millis() - lastTry >= 5000) {
    lastTry = millis();
    mqttConnect();
  }

  mqtt.loop();

  if (activeStudent >= 0 &&
      (long)(millis() - activeUntil) >= 0) {
    activeStudent = -1;
    showIdle();
  }

  if (!rfid.PICC_IsNewCardPresent() ||
      !rfid.PICC_ReadCardSerial()) {
    return;
  }

  Serial.print("UID:");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.printf(" %02X", rfid.uid.uidByte[i]);
  }
  Serial.println();

  bool fourBytes = (rfid.uid.size == 4);
  int studentIndex = fourBytes ? findStudent(rfid.uid.uidByte) : -1;
  int itemIndex = fourBytes ? findItem(rfid.uid.uidByte) : -1;

  // 1. Student ID scanned.
  if (studentIndex >= 0) {
    activeStudent = studentIndex;
    activeUntil = millis() + 10000;

    show("HELLO",
         students[studentIndex].name,
         "SCAN EQUIPMENT",
         "WITHIN 10 SECONDS");

    beep(1200, 100);
    finishCard();
    delay(800);
    return;
  }

  // 2. Unknown RFID tag.
  if (itemIndex < 0) {
    show("ACCESS DENIED", "UNKNOWN ITEM", "", "");
    setLeds(false);
    beep(200, 600);

    if (mqtt.connected()) {
      mqtt.publish(topic("alerts").c_str(), "unknown tag scanned");
    }
  }

  // 3. Equipment checkout.
  else if (!items[itemIndex].borrowed) {
    if (activeStudent < 0) {
      show("SCAN STUDENT ID", "FIRST", items[itemIndex].name, "");
      beep(400, 300);
    } else {
      items[itemIndex].borrowed = true;
      items[itemIndex].by = students[activeStudent].name;

      show("CHECKOUT SUCCESS",
           items[itemIndex].name,
           items[itemIndex].by,
           "STATUS: BORROWED");

      setLeds(false);
      beep(1500, 150);
      publishItem(itemIndex, "checkout");
      activeStudent = -1;
    }
  }

  // 4. Equipment return.
  else {
    const char* previousBorrower = items[itemIndex].by;

    show("RETURN SUCCESS",
         items[itemIndex].name,
         previousBorrower ? previousBorrower : "-",
         "STATUS: AVAILABLE");

    items[itemIndex].borrowed = false;

    setLeds(true);
    beep(1000, 100);
    beep(1800, 150);

    publishItem(itemIndex, "return");
    items[itemIndex].by = nullptr;
  }

  finishCard();
  delay(2000);
  showIdle();
}
