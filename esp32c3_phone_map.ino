/*
  ESP32-C3 Super Mini + OLED SSD1306 128x64 — รับแผนที่เรียลไทม์จากโทรศัพท์ผ่าน BLE

  ต่อสาย OLED:  VCC->3.3V  GND->GND  SDA->GPIO8  SCL->GPIO9
  (ถ้าจอไม่ติด ลองสลับ PIN_SDA / PIN_SCL)

  Arduino IDE:
    - Board: "ESP32C3 Dev Module" (ESP32 core 3.x)
    - USB CDC On Boot: Enabled
    - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"  <- BLE ใช้ Flash เยอะ
    - ไลบรารี: U8g2  (BLE มากับ ESP32 core อยู่แล้ว)

  โปรโตคอล (โทรศัพท์ -> ESP32, เขียนลง characteristic RX):
    [0x01, offHi, offLo, data...]  = ชิ้นส่วนเฟรม 1024 ไบต์ (รูปแบบบัฟเฟอร์ U8g2 ตรง ๆ)
    [0x02, ascii...]               = ข้อความแถบสถานะด้านล่าง
*/
#include <U8g2lib.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define PIN_SDA 8
#define PIN_SCL 9

#define DEVICE_NAME "C3-Map"
#define SVC_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define RX_UUID  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);

static uint8_t backBuf[1024];
static uint8_t frontBuf[1024];
static char statusTxt[32] = "";
static volatile bool frameReady = false;
static volatile bool connected = false;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override { connected = true; }
  void onDisconnect(BLEServer *s) override { connected = false; }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    uint8_t *d = c->getData();
    size_t n = c->getLength();
    if (n < 1) return;

    if (d[0] == 0x01 && n > 3) {                 // ชิ้นส่วนเฟรม
      uint16_t off = (d[1] << 8) | d[2];
      if (off >= 1024) return;
      size_t len = n - 3;
      if (off + len > 1024) len = 1024 - off;
      memcpy(backBuf + off, d + 3, len);
      if (off + len >= 1024) {                    // ชิ้นสุดท้าย -> พร้อมแสดง
        portENTER_CRITICAL(&mux);
        memcpy(frontBuf, backBuf, 1024);
        frameReady = true;
        portEXIT_CRITICAL(&mux);
      }
    } else if (d[0] == 0x02) {                    // ข้อความสถานะ
      size_t len = n - 1;
      if (len > sizeof(statusTxt) - 1) len = sizeof(statusTxt) - 1;
      portENTER_CRITICAL(&mux);
      memcpy(statusTxt, d + 1, len);
      statusTxt[len] = 0;
      portEXIT_CRITICAL(&mux);
    }
  }
};

void setupBLE() {
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setMTU(247);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  BLEService *svc = server->createService(SVC_UUID);
  BLECharacteristic *rx = svc->createCharacteristic(
      RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCB());
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void drawMarker(bool blink) {
  u8g2.setDrawColor(0);
  u8g2.drawDisc(64, 32, 5);
  u8g2.setDrawColor(1);
  u8g2.drawCircle(64, 32, 5);
  if (blink) u8g2.drawDisc(64, 32, 2);
}

void drawStatus(const char *txt) {
  if (!txt[0]) return;
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 56, 128, 8);
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(1, 63, txt);
}

void drawWaiting(bool conn) {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 12, "C3-Map  (BLE)");
  u8g2.drawHLine(0, 15, 128);
  if (conn) {
    u8g2.drawStr(0, 32, "Phone connected");
    u8g2.drawStr(0, 46, "Waiting for map...");
  } else {
    u8g2.drawStr(0, 32, "Open web page on");
    u8g2.drawStr(0, 44, "phone & connect:");
    u8g2.drawStr(0, 58, DEVICE_NAME);
  }
}

void setup() {
  Serial.begin(115200);
  u8g2.setBusClock(400000);
  u8g2.begin();
  setupBLE();
}

void loop() {
  static uint8_t view[1024];
  static bool haveFrame = false;
  static bool wasConnected = false;
  char st[32];

  // เมื่อหลุดการเชื่อมต่อ ให้กลับไปประกาศตัว (advertise) ใหม่
  if (wasConnected && !connected) {
    delay(200);
    BLEDevice::startAdvertising();
  }
  wasConnected = connected;

  portENTER_CRITICAL(&mux);
  if (frameReady) {
    memcpy(view, frontBuf, 1024);
    frameReady = false;
    haveFrame = true;
  }
  strcpy(st, statusTxt);
  portEXIT_CRITICAL(&mux);

  u8g2.clearBuffer();
  if (haveFrame) {
    memcpy(u8g2.getBufferPtr(), view, 1024);
    drawMarker((millis() / 400) % 2);
    drawStatus(connected ? st : "BLE disconnected");
  } else {
    drawWaiting(connected);
  }
  u8g2.sendBuffer();
  delay(40);
}
