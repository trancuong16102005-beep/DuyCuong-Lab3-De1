#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <PubSubClient.h>

// ---------------- Cấu hình phần cứng ----------------
#define LED_PIN     14      // đổi theo đúng chân LED trong mạch Wokwi của bạn
#define BUZZER_PIN  19
#define BUZZER_FREQ 2000    // Hz

// ---------------- Cấu hình mạng ----------------
// Tên mạng WiFi ảo của trình mô phỏng: Wokwi phát "Wokwi-GUEST", Cirkit Designer phát "CirkitWifi".
// Task truyền quét mạng lúc khởi động và tự chọn tên có thật (xem TaskTruyenData).
const char* WIFI_SSID_WOKWI  = "Wokwi-GUEST";
const char* WIFI_SSID_CIRKIT = "CirkitWifi";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;
// Đổi tiền tố topic cho riêng bạn để không trùng người khác trên broker công cộng
const char* TOPIC_DATA = "lad3/runner/data";   // ESP32 -> web
const char* TOPIC_CMD  = "lad3/runner/cmd";    // web   -> ESP32

// ---------------- Ngưỡng cảnh báo ----------------
const float T_MAX = 37.5;              // °C, nhiệt độ quá ngưỡng
const float T_MIN = 15.0;              // °C, nhiệt độ dưới ngưỡng
const float ALT_JUMP = 3.0;            // m, thay đổi so với trung bình 3 lần đo trước => đột ngột
const uint32_t ALT_HOLD_MS = 3000;     // giữ còi 3 s sau biến cố độ cao
const uint32_t MANUAL_TIMEOUT_MS = 15000; // chế độ thủ công tự hết sau 15 s
const float SEA_LEVEL_PA = 101325.0;   // áp suất chuẩn mực nước biển
const uint32_t READ_PERIOD_MS = 1000;  // chu kỳ đọc cảm biến

// ---------------- Kiểu dữ liệu ----------------
typedef struct { float t; float p; float h; } DuLieu;      // gói đầy đủ (qData)
typedef struct { float t; float h; } CanhBao;              // gói cảnh báo (qAlert)
typedef enum { CMD_LED_ON, CMD_LED_OFF, CMD_BUZ_ON, CMD_BUZ_OFF, CMD_AUTO } Lenh;
typedef enum { R_NONE, R_TEMP_HIGH, R_TEMP_LOW, R_ALTITUDE, R_MANUAL } LyDo;
typedef struct { bool led; bool buzzer; bool manual; uint8_t reason; } TrangThai;

// ---------------- Đối tượng FreeRTOS ----------------
QueueHandle_t qData, qAlert, qLenh, qTrangThai;
SemaphoreHandle_t semWiFi, mSerial;

Adafruit_BMP085 bmp;
WiFiClient netClient;
PubSubClient mqtt(netClient);

// ---------------- Serial dùng chung (mutex) ----------------
void logLine(const char* fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (xSemaphoreTake(mSerial, pdMS_TO_TICKS(50)) == pdTRUE) {
    Serial.println(buf);
    xSemaphoreGive(mSerial);
  }
}

// ---------------- Sự kiện WiFi -> giơ cờ semWiFi ----------------
// Hàm này chạy trong task sự kiện của hệ thống (không phải ngắt), nên dùng xSemaphoreGive thường.
void onWiFiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    logLine("[WIFI] Su kien GOT_IP, IP = %s", WiFi.localIP().toString().c_str());
    xSemaphoreGive(semWiFi);
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    logLine("[WIFI] Su kien mat ket noi WiFi");
  }
}

// ---------------- Nhận lệnh MQTT (chạy bên trong Task truyền) ----------------
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  char msg[24];
  unsigned int n = len < sizeof(msg) - 1 ? len : sizeof(msg) - 1;
  memcpy(msg, payload, n);
  msg[n] = '\0';

  Lenh cmd;
  if      (strcmp(msg, "led_on") == 0)     cmd = CMD_LED_ON;
  else if (strcmp(msg, "led_off") == 0)    cmd = CMD_LED_OFF;
  else if (strcmp(msg, "buzzer_on") == 0)  cmd = CMD_BUZ_ON;
  else if (strcmp(msg, "buzzer_off") == 0) cmd = CMD_BUZ_OFF;
  else if (strcmp(msg, "auto") == 0)       cmd = CMD_AUTO;
  else { logLine("[TRUYEN] Lenh khong hop le: %s", msg); return; }

  // Không chạm vào LED/còi ở đây: chỉ chuyển lệnh cho Task cảnh báo
  xQueueSend(qLenh, &cmd, 0);
  logLine("[TRUYEN] Nhan lenh: %s", msg);
}

// =====================================================================
// TASK ĐỌC DATA - Core 1, ưu tiên 2
// =====================================================================
void TaskDocData(void* pv) {
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    DuLieu d;
    d.t = bmp.readTemperature();
    long pa = bmp.readPressure();                  // Pa
    d.p = pa / 100.0f;                             // hPa
    d.h = 44330.0f * (1.0f - powf(pa / SEA_LEVEL_PA, 0.1903f)); // độ cao (m)

    // 1. Gửi dữ liệu cảnh báo TRƯỚC: Task cảnh báo (ưu tiên 3) sẽ chen ngang xử lý ngay
    CanhBao a = { d.t, d.h };
    xQueueSend(qAlert, &a, 0);

    // 2. In Serial (có khóa mSerial)
    logLine("[DOC] T=%.1f C | P=%.1f hPa | h=%.1f m", d.t, d.p, d.h);

    // 3. Gửi gói đầy đủ cho Task truyền; đầy (mất mạng) thì bỏ gói cũ nhất, giữ gói mới
    if (xQueueSend(qData, &d, 0) != pdTRUE) {
      DuLieu cu;
      xQueueReceive(qData, &cu, 0);
      xQueueSend(qData, &d, 0);
    }

    // 4. Ngủ đến đúng nhịp 1 s tiếp theo (không trôi chu kỳ)
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(READ_PERIOD_MS));
  }
}

// =====================================================================
// TASK CẢNH BÁO - Core 1, ưu tiên 3 (chủ duy nhất của LED và còi)
// =====================================================================
static bool ledOn = false, buzOn = false;

void apDungDauRa(bool led, bool buz) {
  if (led && buz) led = false;                     // luật: chỉ một trong hai, còi ưu tiên
  if (led != ledOn) { digitalWrite(LED_PIN, led ? HIGH : LOW); ledOn = led; }
  if (buz != buzOn) { if (buz) tone(BUZZER_PIN, BUZZER_FREQ); else noTone(BUZZER_PIN); buzOn = buz; }
}

void TaskCanhBao(void* pv) {
  float hist[3];
  int nHist = 0;
  uint32_t altUntil = 0;        // còi giữ đến thời điểm này (ms)
  bool manual = false;
  bool manLed = false, manBuz = false;
  uint32_t manualUntil = 0;
  float lastT = 25.0;

  TrangThai st = { false, false, false, R_NONE };
  xQueueOverwrite(qTrangThai, &st);               // hộp thư luôn có giá trị ban đầu

  for (;;) {
    CanhBao a;
    bool coDuLieu = xQueueReceive(qAlert, &a, pdMS_TO_TICKS(200)) == pdTRUE;
    uint32_t now = millis();

    if (coDuLieu) {
      lastT = a.t;
      // Phát hiện độ cao đổi đột ngột: so với trung bình 3 lần đo trước (lọc nhiễu)
      if (nHist == 3) {
        float avg = (hist[0] + hist[1] + hist[2]) / 3.0f;
        if (fabsf(a.h - avg) > ALT_JUMP) {
          altUntil = now + ALT_HOLD_MS;
          manual = false;                         // biến cố mới luôn giành lại quyền tự động
          for (int i = 0; i < 3; i++) hist[i] = a.h;   // làm mới lịch sử, tránh báo lặp
          nHist = 3;
        } else {
          hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = a.h;
        }
      } else {
        hist[nHist++] = a.h;
      }
    }

    // Lệnh từ web: xem qLenh, không chờ
    Lenh cmd;
    while (xQueueReceive(qLenh, &cmd, 0) == pdTRUE) {
      if (cmd == CMD_AUTO) { manual = false; continue; }
      if (!manual) { manLed = ledOn; manBuz = buzOn; } // vừa vào thủ công: bắt đầu từ trạng thái đèn/còi hiện tại
      manual = true;
      manualUntil = now + MANUAL_TIMEOUT_MS;
      if (cmd == CMD_LED_ON)  { manLed = true;  manBuz = false; }
      if (cmd == CMD_LED_OFF) { manLed = false; }
      if (cmd == CMD_BUZ_ON)  { manBuz = true;  manLed = false; }
      if (cmd == CMD_BUZ_OFF) { manBuz = false; }
    }
    if (manual && (int32_t)(now - manualUntil) >= 0) manual = false;

    // Quyết định đầu ra
    bool led, buz;
    uint8_t reason;
    if (manual) {
      led = manLed; buz = manBuz; reason = R_MANUAL;
    } else {
      bool altAlarm = (int32_t)(altUntil - now) > 0;
      bool hot = lastT > T_MAX, cold = lastT < T_MIN;
      if (altAlarm)          { led = false; buz = true;  reason = R_ALTITUDE; }
      else if (hot || cold)  { led = true;  buz = false; reason = hot ? R_TEMP_HIGH : R_TEMP_LOW; }
      else                   { led = false; buz = false; reason = R_NONE; }
    }
    apDungDauRa(led, buz);

    // Ghi đè trạng thái thật vào hộp thư cho Task truyền
    TrangThai moi = { ledOn, buzOn, manual, reason };
    if (moi.led != st.led || moi.buzzer != st.buzzer || moi.manual != st.manual || moi.reason != st.reason) {
      st = moi;
      xQueueOverwrite(qTrangThai, &st);
    }
  }
}

// =====================================================================
// TASK TRUYỀN DATA - Core 0, ưu tiên 1
// =====================================================================
const char* tenLyDo(uint8_t r) {
  switch (r) {
    case R_TEMP_HIGH: return "temp_high";
    case R_TEMP_LOW:  return "temp_low";
    case R_ALTITUDE:  return "altitude";
    case R_MANUAL:    return "manual";
    default:          return "none";
  }
}

bool ketNoiMQTT() {
  char clientId[32];
  snprintf(clientId, sizeof(clientId), "runner-esp32-%04X", (unsigned)(esp_random() & 0xFFFF));
  if (mqtt.connect(clientId)) {
    mqtt.subscribe(TOPIC_CMD);
    logLine("[TRUYEN] Da ket noi MQTT broker %s", MQTT_HOST);
    return true;
  }
  logLine("[TRUYEN] Ket noi MQTT loi (rc=%d), thu lai sau 2 s", mqtt.state());
  return false;
}

void TaskTruyenData(void* pv) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWiFiEvent);

  // Chẩn đoán: liệt kê các mạng WiFi mà trình mô phỏng đang phát (tên và kênh)
  // Quét mạng rồi chọn đúng tên mạng mà trình mô phỏng đang phát
  const char* ssid = WIFI_SSID_WOKWI;
  int n = WiFi.scanNetworks();
  logLine("[WIFI] Quet thay %d mang", n);
  for (int i = 0; i < n; i++) {
    logLine("[WIFI]  - %s (kenh %d)", WiFi.SSID(i).c_str(), WiFi.channel(i));
    if (WiFi.SSID(i) == WIFI_SSID_CIRKIT) ssid = WIFI_SSID_CIRKIT;
  }
  WiFi.scanDelete();
  logLine("[WIFI] Ket noi vao mang: %s", ssid);

  WiFi.begin(ssid, WIFI_PASS);                    // không ép kênh: tự tìm mạng trên mọi kênh

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);

  for (;;) {
    // "Kết nối WiFi, nếu thành công..." : chưa có WiFi thì ngủ chờ cờ semWiFi
    if (WiFi.status() != WL_CONNECTED) {
      logLine("[TRUYEN] Cho WiFi... (status=%d)", WiFi.status());
      xSemaphoreTake(semWiFi, pdMS_TO_TICKS(2000));  // chờ cờ tối đa 2 s rồi tự kiểm tra lại
      continue;                                   // kiểm tra lại trạng thái rồi mới đi tiếp
    }
    if (!mqtt.connected()) {
      if (!ketNoiMQTT()) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
    }

    mqtt.loop();                                  // giữ kết nối và nhận lệnh (gọi onMqttMessage)

    // Chờ dữ liệu tối đa 100 ms để còn quay lại gọi mqtt.loop() đều đặn
    DuLieu d;
    if (xQueueReceive(qData, &d, pdMS_TO_TICKS(100)) == pdTRUE) {
      TrangThai st;
      xQueuePeek(qTrangThai, &st, 0);             // xem trạng thái, không lấy ra
      char json[200];
      snprintf(json, sizeof(json),
               "{\"t\":%.1f,\"p\":%.1f,\"h\":%.1f,\"led\":%d,\"buzzer\":%d,\"mode\":\"%s\",\"alert\":\"%s\"}",
               d.t, d.p, d.h, st.led, st.buzzer, st.manual ? "manual" : "auto", tenLyDo(st.reason));
      if (mqtt.publish(TOPIC_DATA, json)) logLine("[TRUYEN] Gui: %s", json);
    }
  }
}

// =====================================================================
// SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);

  // Tạo đối tượng giao tiếp TRƯỚC khi tạo task
  qData      = xQueueCreate(5, sizeof(DuLieu));
  qAlert     = xQueueCreate(5, sizeof(CanhBao));
  qLenh      = xQueueCreate(3, sizeof(Lenh));
  qTrangThai = xQueueCreate(1, sizeof(TrangThai));
  semWiFi    = xSemaphoreCreateBinary();
  mSerial    = xSemaphoreCreateMutex();

  if (!bmp.begin()) {
    Serial.println("Khong tim thay BMP180, kiem tra day SDA/SCL!");
    while (true) delay(1000);
  }
  Serial.println("He thong khoi dong...");

  //                       hàm             tên          stack  tham số  ưu tiên handle  core
  xTaskCreatePinnedToCore(TaskCanhBao,    "CanhBao",   4096,  NULL,    3,      NULL,   1);
  xTaskCreatePinnedToCore(TaskDocData,    "DocData",   4096,  NULL,    2,      NULL,   1);
  xTaskCreatePinnedToCore(TaskTruyenData, "TruyenData",8192,  NULL,    1,      NULL,   0);
}

void loop() {
  vTaskDelete(NULL);   // loopTask không dùng đến, tự xóa để nhường CPU
}
