/*
 * Humstrava — прошивка ESP32
 *
 * Что делает:
 *  - ловит щелчки геркона при проходе магнита (ISR + дебаунс 100 мс)
 *  - копит тики в кольцевой буфер (12 800 записей, ~50 KB)
 *  - синхронизирует время через NTP (fallback — GET /time у бэкенда)
 *  - раз в секунду шлёт батч на POST /ticks с заголовком X-API-Key
 *
 * Физика:
 *  - геркон между REED_PIN и GND
 *  - INPUT_PULLUP: норма HIGH, магнит близко → LOW
 *  - ISR срабатывает на FALLING (HIGH→LOW)
 *
 * См. docs/decisions.md и docs/open-questions.md
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <sys/time.h>

// ================== НАСТРОЙКИ ==================

const char* WIFI_SSID = "-cVr7";
const char* WIFI_PASS = "";

// При локальном тестировании укажи IP ноутбука в LAN, например http://192.168.1.42:8000
const char* API_BASE  = "https://api.hamstrava.app";

const char* API_KEY   = "";
const char* DEVICE_ID = "hamster-wheel";

const int  REED_PIN = 4;

// Дебаунс: тики ближе DEBOUNCE_MS игнорируются (OQ-007).
// Минимум между настоящими тиками 244 мс (Роборовский на максимуме), 100 мс безопасно.
const uint32_t DEBOUNCE_MS = 100;

// Кольцевой буфер. 12 800 записей × 4 байта = ~50 KB.
// При переполнении вытесняем старые (OQ-001).
const size_t BUFFER_SIZE = 12800;

// Максимум тиков в одном HTTP-батче. Если буфер больше — дренируем по 200 за раз.
const size_t MAX_BATCH = 200;

// Раз в секунду отправляем батч, если есть тики.
const uint32_t BATCH_INTERVAL_MS = 1000;

// NTP
const char*   NTP_SERVER     = "pool.ntp.org";
const uint32_t NTP_TIMEOUT_MS = 5000;

// HTTP timeout для запросов на бэкенд
const uint32_t HTTP_TIMEOUT_MS = 5000;

// ================== СОСТОЯНИЕ ==================

// Ring buffer хранит boot_ms (millis() на момент тика).
// Конвертация в unix_ms делается при отправке через unix_offset_ms.
volatile uint32_t tickBuffer[BUFFER_SIZE];
volatile size_t   bufHead = 0;   // куда писать
volatile size_t   bufTail = 0;   // откуда читать
volatile size_t   bufCount = 0;
portMUX_TYPE      bufMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t lastTickMs = 0;  // для дебаунса в ISR

// Смещение для конвертации millis() → unix_ms.
// После NTP-синхронизации: unixOffsetMs = current_unix_ms - millis()
int64_t  unixOffsetMs = 0;
bool     timeSynced   = false;

uint32_t lastBatchAttemptMs = 0;

// HTTPS клиент с пропуском проверки сертификата (для MVP).
// Для прода стоит запекать root CA, чтобы не доверять любому.
WiFiClientSecure secureClient;
WiFiClient       plainClient;

// ================== ISR ==================

void IRAM_ATTR reedISR() {
  uint32_t now = millis();
  if (now - lastTickMs < DEBOUNCE_MS) return;
  lastTickMs = now;

  portENTER_CRITICAL_ISR(&bufMux);
  tickBuffer[bufHead] = now;
  bufHead = (bufHead + 1) % BUFFER_SIZE;
  if (bufCount < BUFFER_SIZE) {
    bufCount++;
  } else {
    // буфер полный — вытесняем старый
    bufTail = (bufTail + 1) % BUFFER_SIZE;
  }
  portEXIT_CRITICAL_ISR(&bufMux);
}

// ================== WIFI ==================

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("Connecting to WiFi %s...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi OK, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi connect failed, will retry");
  }
}

// ================== TIME SYNC ==================

bool syncFromNTP() {
  Serial.println("Syncing via NTP...");
  configTime(0, 0, NTP_SERVER);

  uint32_t start = millis();
  struct tm tmInfo;
  while (millis() - start < NTP_TIMEOUT_MS) {
    if (getLocalTime(&tmInfo, 200)) {
      struct timeval tv;
      gettimeofday(&tv, nullptr);
      int64_t unixMs = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
      unixOffsetMs = unixMs - (int64_t)millis();
      Serial.printf("NTP synced, unix_offset_ms=%lld\n", (long long)unixOffsetMs);
      return true;
    }
  }
  Serial.println("NTP timeout");
  return false;
}

bool syncFromBackend() {
  Serial.println("Trying backend /time fallback...");
  HTTPClient http;
  String url = String(API_BASE) + "/time";
  bool ok = strncmp(API_BASE, "https://", 8) == 0
    ? http.begin(secureClient, url)
    : http.begin(plainClient, url);
  if (!ok) {
    Serial.println("http.begin failed");
    return false;
  }
  http.setTimeout(HTTP_TIMEOUT_MS);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("/time returned %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  // {"ms":1748945201123}
  int colon = body.indexOf(':');
  int close = body.indexOf('}');
  if (colon < 0 || close < 0 || close <= colon) return false;
  int64_t serverMs = strtoll(body.substring(colon + 1, close).c_str(), nullptr, 10);
  if (serverMs <= 0) return false;

  unixOffsetMs = serverMs - (int64_t)millis();
  Serial.printf("Backend time synced, unix_offset_ms=%lld\n", (long long)unixOffsetMs);
  return true;
}

void syncTime() {
  if (syncFromNTP() || syncFromBackend()) {
    timeSynced = true;
  } else {
    Serial.println("Time sync FAILED — ticks won't be sent until synced");
  }
}

// ================== BATCH SEND ==================

void sendBatch() {
  uint32_t batch[MAX_BATCH];
  size_t batchSize = 0;

  // Снимок буфера под mutex'ом
  portENTER_CRITICAL(&bufMux);
  size_t toSend = bufCount < MAX_BATCH ? bufCount : MAX_BATCH;
  size_t idx = bufTail;
  for (size_t i = 0; i < toSend; i++) {
    batch[i] = tickBuffer[idx];
    idx = (idx + 1) % BUFFER_SIZE;
  }
  batchSize = toSend;
  portEXIT_CRITICAL(&bufMux);

  if (batchSize == 0) return;

  // JSON: {"d":"hamster-wheel","ticks":[unix_ms_1,unix_ms_2,...]}
  String body;
  body.reserve(64 + batchSize * 16);
  body += "{\"d\":\"";
  body += DEVICE_ID;
  body += "\",\"ticks\":[";
  for (size_t i = 0; i < batchSize; i++) {
    if (i > 0) body += ',';
    int64_t unixMs = (int64_t)batch[i] + unixOffsetMs;
    body += String((long long)unixMs);
  }
  body += "]}";

  // POST
  HTTPClient http;
  String url = String(API_BASE) + "/ticks";
  bool ok = strncmp(API_BASE, "https://", 8) == 0
    ? http.begin(secureClient, url)
    : http.begin(plainClient, url);
  if (!ok) {
    Serial.println("http.begin failed (POST)");
    return;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);
  http.setTimeout(HTTP_TIMEOUT_MS);

  int code = http.POST(body);
  String resp = code == 200 ? http.getString() : String();
  http.end();

  if (code == 200) {
    // Снимаем с буфера ровно столько, сколько отправили.
    // Если ISR вытеснил часть наших тиков во время POST'а (буфер переполнился) —
    // bufCount может оказаться меньше batchSize. Тогда буфер уже пуст по этим тикам.
    portENTER_CRITICAL(&bufMux);
    size_t toRemove = bufCount < batchSize ? bufCount : batchSize;
    bufTail = (bufTail + toRemove) % BUFFER_SIZE;
    bufCount -= toRemove;
    portEXIT_CRITICAL(&bufMux);
    Serial.printf("sent %u ticks, resp: %s\n", (unsigned)batchSize, resp.c_str());
  } else {
    // Не очищаем буфер — повторим в следующей итерации (OQ-001)
    Serial.printf("POST failed: %d, keeping %u ticks in buffer\n", code, (unsigned)batchSize);
  }
}

// ================== SETUP / LOOP ==================

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Humstrava firmware ===");

  pinMode(REED_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(REED_PIN), reedISR, FALLING);

  secureClient.setInsecure();  // MVP: пропускаем валидацию TLS-сертификата

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    syncTime();
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    delay(1000);
    return;
  }

  if (!timeSynced) {
    syncTime();
    if (!timeSynced) {
      delay(5000);
      return;
    }
  }

  uint32_t now = millis();
  if (now - lastBatchAttemptMs >= BATCH_INTERVAL_MS) {
    lastBatchAttemptMs = now;
    sendBatch();
  }
}
