/*
 * Hamster Strava — Шаг 2+3: точная скорость + Wi-Fi дашборд
 * ----------------------------------------------------------
 * Железо: ESP32 + геркон (GPIO 4 <-> GND) + магнит на колесе.
 *
 * Что нового по сравнению с шагом 1:
 *  1) Скорость считается по ВРЕМЕНИ МЕЖДУ ДВУМЯ ЩЕЛЧКАМИ магнита
 *     (твоя идея): v = длина_окружности / интервал. Плавно даже на медленном беге.
 *  2) Если магнита нет дольше idleTimeoutMs — считаем, что хомяк остановился (v = 0).
 *  3) Лёгкое сглаживание скорости по нескольким последним оборотам.
 *  4) ESP32 поднимает Wi-Fi и отдаёт веб-страничку (дашборд) —
 *     открываешь её в браузере телефона, Serial Monitor больше не нужен.
 *
 * КАК ПОЛЬЗОВАТЬСЯ:
 *  - впиши имя сети и пароль ниже (WIFI_SSID / WIFI_PASS),
 *  - залей скетч, открой Serial Monitor один раз: плата напечатает IP-адрес,
 *  - набери этот адрес в браузере телефона (телефон в той же Wi-Fi сети).
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ================== НАСТРОЙКИ ==================
const char* WIFI_SSID = "DIGIFIBRA-cVr7";   // <-- впиши имя своей Wi-Fi сети (2.4 ГГц)
const char* WIFI_PASS = "EpDMqFbguKa9";       // <-- впиши пароль

const int   REED_PIN          = 4;       // геркон на GPIO 4
const float wheelDiameterCm   = 20.0;    // <-- ДИАМЕТР беговой дорожки колеса, см (поправь под своё!)
const int   magnetsCount      = 1;       // сколько магнитов на колесе (пока 1)

const unsigned long debounceMs     = 30;     // антидребезг геркона
const unsigned long idleTimeoutMs  = 2500;   // нет щелчков дольше -> скорость = 0 (хомяк отдыхает)
const int   smoothingSamples       = 4;      // по скольким последним оборотам усредняем скорость

// ================== ПРОИЗВОДНЫЕ ==================
const float wheelCircumferenceM = 3.14159265 * wheelDiameterCm / 100.0;
// путь за один ЩЕЛЧОК (если магнитов несколько, один оборот = несколько щелчков):
const float distancePerPulseM   = wheelCircumferenceM / magnetsCount;

// ================== СОСТОЯНИЕ ==================
volatile unsigned long pulses = 0;            // всего щелчков магнита
volatile unsigned long lastPulseMs = 0;       // когда был последний щелчок
volatile bool magnetWasNear = false;

// для расчёта скорости по интервалам
unsigned long intervalBuf[16];                // кольцевой буфер последних интервалов (мс)
int   intervalIdx = 0;
int   intervalFilled = 0;
unsigned long prevPulseForSpeed = 0;          // время предыдущего щелчка (для интервала)

WebServer server(80);

// --- вернуть текущую скорость в км/ч ---
float currentSpeedKmh() {
  // если давно не было щелчков — хомяк стоит
  if (millis() - lastPulseMs > idleTimeoutMs || intervalFilled == 0) return 0.0;

  // среднее по последним интервалам
  int n = min(intervalFilled, smoothingSamples);
  unsigned long sum = 0;
  for (int i = 0; i < n; i++) {
    int idx = (intervalIdx - 1 - i + 16) % 16;
    sum += intervalBuf[idx];
  }
  float avgMs = (float)sum / n;
  if (avgMs <= 0) return 0.0;

  float speedMs = distancePerPulseM / (avgMs / 1000.0);  // путь за щелчок / время за щелчок
  return speedMs * 3.6;
}

float totalDistanceM() {
  return pulses * distancePerPulseM;
}

// --- веб-страница дашборда ---
String buildPage() {
  float dist = totalDistanceM();
  float speed = currentSpeedKmh();

  String html = F(
    "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='2'>"  // автообновление раз в 2 сек
    "<title>Hamster Strava</title><style>"
    "body{margin:0;font-family:-apple-system,system-ui,sans-serif;background:#fc4c02;color:#fff;"
    "display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:100vh}"
    ".card{background:rgba(0,0,0,.15);border-radius:24px;padding:32px 40px;text-align:center;margin:10px}"
    ".big{font-size:64px;font-weight:800;line-height:1}"
    ".unit{font-size:22px;opacity:.85}"
    ".label{font-size:14px;text-transform:uppercase;letter-spacing:2px;opacity:.8;margin-bottom:8px}"
    "h1{font-weight:800;letter-spacing:1px}"
    "</style></head><body>"
    "<h1>🐹 Hamster Strava</h1>"
  );
  html += "<div class='card'><div class='label'>Дистанция</div><div class='big'>";
  html += String(dist, 1);
  html += "<span class='unit'> м</span></div></div>";

  html += "<div class='card'><div class='label'>Скорость</div><div class='big'>";
  html += String(speed, 2);
  html += "<span class='unit'> км/ч</span></div></div>";

  html += "<div class='card'><div class='label'>Оборотов</div><div class='big'>";
  html += String(pulses / magnetsCount);
  html += "</div></div>";

  html += "</body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", buildPage());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(REED_PIN, INPUT_PULLUP);

  Serial.println("\n=== Hamster Strava — Wi-Fi дашборд ===");
  Serial.print("Подключаюсь к Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi подключён!");
    Serial.print(">>> Адрес по IP:  http://");
    Serial.println(WiFi.localIP());

    // Понятное имя в сети: можно заходить по http://hamster.local
    if (MDNS.begin("hamster")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println(">>> Или просто открой:  http://hamster.local");
    }
  } else {
    Serial.println("Не удалось подключиться к Wi-Fi. Проверь имя сети и пароль (и что сеть 2.4 ГГц).");
  }

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();

  // --- детект щелчка магнита с антидребезгом ---
  bool magnetNear = (digitalRead(REED_PIN) == LOW);
  unsigned long now = millis();

  if (magnetNear && !magnetWasNear && (now - lastPulseMs > debounceMs)) {
    // интервал от прошлого щелчка
    if (prevPulseForSpeed != 0) {
      unsigned long interval = now - prevPulseForSpeed;
      intervalBuf[intervalIdx] = interval;
      intervalIdx = (intervalIdx + 1) % 16;
      if (intervalFilled < 16) intervalFilled++;
    }
    prevPulseForSpeed = now;
    lastPulseMs = now;
    pulses++;
  }
  magnetWasNear = magnetNear;
}
