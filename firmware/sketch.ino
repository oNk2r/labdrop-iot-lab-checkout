#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ---------- Pins ----------
#define SS_PIN    5
#define RST_PIN   4
#define LED_GREEN 26
#define LED_RED   27
#define BUZZER    25

// ---------- Network / MQTT ----------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
// MUST match TOPIC_ROOT in dashboard.html. Make it unique, the broker is public.
const char* ROOT = "labdrop-CHANGE-ME";

WiFiClient net;
PubSubClient mqtt(net);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------- Equipment database ----------
struct Equipment {
  const char* id;
  const char* name;
  byte uid[4];
  bool borrowed;
  const char* by;   // who borrowed it (nullptr when available)
};

struct Student { const char* name; byte uid[4]; };

// Replace with UIDs of OTHER Wokwi cards (not the equipment ones)
Student students[] = {
  { "Jack", {0xAA, 0xBB, 0xCC, 0xDD} },
  { "Asha", {0x55, 0x66, 0x77, 0x88} },
};
const int STUDENT_COUNT = sizeof(students) / sizeof(students[0]);
int activeStudent = -1;           // student who scanned ID and is waiting to scan equipment
unsigned long activeUntil = 0;    // ID scan expires after 10 s

// Replace UIDs with the ones your Wokwi cards give you (see Serial Monitor)
Equipment items[] = {
  { "024", "ESP32 Kit #024",   {0xDE, 0xAD, 0xBE, 0xEF}, false },
  { "011", "Arduino Kit #011", {0x01, 0x02, 0x03, 0x04}, false },
  { "007", "Sensor Kit #007",  {0x11, 0x22, 0x33, 0x44}, false },
};
const int ITEM_COUNT = sizeof(items) / sizeof(items[0]);

// ---------- Helpers ----------
void beep(int f, int ms) { tone(BUZZER, f, ms); delay(ms); noTone(BUZZER); }

void setLeds(bool green) {
  digitalWrite(LED_GREEN, green);
  digitalWrite(LED_RED, !green);
}

void show(const char* a, const char* b, const char* c, const char* d) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);  oled.println(a);
  oled.setCursor(0, 16); oled.println(b);
  oled.setCursor(0, 32); oled.println(c);
  oled.setCursor(0, 48); oled.println(d);
  oled.display();
}

void showIdle() {
  int out = 0;
  for (int i = 0; i < ITEM_COUNT; i++) if (items[i].borrowed) out++;
  char l3[24], l4[24];
  snprintf(l3, sizeof(l3), "AVAILABLE: %d", ITEM_COUNT - out);
  snprintf(l4, sizeof(l4), "BORROWED : %d", out);
  show("LABDROP", mqtt.connected() ? "SCAN TAG  [online]" : "SCAN TAG [offline]", l3, l4);
  setLeds(out == 0);
}

int findItem(byte* uid) {
  for (int i = 0; i < ITEM_COUNT; i++)
    if (memcmp(uid, items[i].uid, 4) == 0) return i;
  return -1;
}

int findStudent(byte* uid) {
  for (int i = 0; i < STUDENT_COUNT; i++)
    if (memcmp(uid, students[i].uid, 4) == 0) return i;
  return -1;
}

// ---------- MQTT ----------
String topic(const char* suffix) { return String(ROOT) + "/" + suffix; }

// Retained per-item state: the dashboard gets the current picture on connect
void publishItem(int i, const char* event) {
  if (!mqtt.connected()) return;
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"id\":\"%s\",\"name\":\"%s\",\"status\":\"%s\",\"by\":\"%s\",\"ts\":%ld}",
           items[i].id, items[i].name,
           items[i].borrowed ? "borrowed" : "available",
           items[i].by ? items[i].by : "-", (long)time(nullptr));
  mqtt.publish(topic((String("equipment/") + items[i].id).c_str()).c_str(), buf, true);
  if (event) mqtt.publish(topic("events").c_str(), buf, false);  // activity log (not retained)
}

void mqttConnect() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  String cid = "labdrop-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  String lwt = topic("status");
  // Last Will: broker marks device offline if it drops unexpectedly
  if (mqtt.connect(cid.c_str(), nullptr, nullptr, lwt.c_str(), 0, true, "offline")) {
    mqtt.publish(lwt.c_str(), "online", true);
    for (int i = 0; i < ITEM_COUNT; i++) publishItem(i, nullptr);
    Serial.println("MQTT connected");
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  Wire.begin(21, 22);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { Serial.println("OLED fail"); while (1) delay(1000); }
  SPI.begin(18, 19, 23, SS_PIN);
  rfid.PCD_Init();

  show("LABDROP", "Connecting Wi-Fi...", "", "");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  configTime(19800, 0, "pool.ntp.org");   // IST (UTC+5:30); change offset if needed

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqttConnect();
  showIdle();
}

// ---------- Loop ----------
void finishCard() { rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); }

void loop() {
  static unsigned long lastTry = 0;
  if (!mqtt.connected() && millis() - lastTry > 5000) { lastTry = millis(); mqttConnect(); }
  mqtt.loop();

  if (activeStudent >= 0 && millis() > activeUntil) { activeStudent = -1; showIdle(); }

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  Serial.print("UID:");
  for (byte i = 0; i < rfid.uid.size; i++) Serial.printf(" %02X", rfid.uid.uidByte[i]);
  Serial.println();

  bool four = (rfid.uid.size == 4);
  int st = four ? findStudent(rfid.uid.uidByte) : -1;
  int idx = four ? findItem(rfid.uid.uidByte) : -1;

  if (st >= 0) {                       // student ID card
    activeStudent = st;
    activeUntil = millis() + 10000;
    show("HELLO", students[st].name, "SCAN EQUIPMENT", "(within 10 s)");
    beep(1200, 100);
    finishCard(); delay(800);
    return;
  }

  if (idx < 0) {                       // unknown tag
    show("ACCESS DENIED", "UNKNOWN ITEM", "", "");
    setLeds(false); beep(200, 600);
    if (mqtt.connected()) mqtt.publish(topic("alerts").c_str(), "unknown tag scanned");
  } else if (!items[idx].borrowed) {   // checkout needs a student
    if (activeStudent < 0) {
      show("SCAN STUDENT ID", "FIRST", items[idx].name, "");
      beep(400, 300);
    } else {
      items[idx].borrowed = true;
      items[idx].by = students[activeStudent].name;
      show("CHECKOUT SUCCESS", items[idx].name, items[idx].by, "STATUS: BORROWED");
      setLeds(false); beep(1500, 150);
      publishItem(idx, "checkout");
      activeStudent = -1;
    }
  } else {                             // return (event records the borrower of record)
    items[idx].borrowed = false;
    show("RETURN SUCCESS", items[idx].name, items[idx].by, "STATUS: AVAILABLE");
    setLeds(true); beep(1000, 100); beep(1800, 150);
    publishItem(idx, "return");
    items[idx].by = nullptr;
  }

  finishCard();
  delay(2000);
  showIdle();
}
