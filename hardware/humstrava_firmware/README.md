# Humstrava — ESP32 Firmware

Прошивка ESP32 для умного колеса. Ловит обороты герконом, копит в RAM, шлёт батчами на бэкенд.

## Железо

- ESP32 (любая плата с WiFi, например WROOM-32)
- Геркон между `GPIO 4` и `GND`
- Магнит на колесе (1 шт)

## Логика работы

1. При старте: WiFi → NTP (fallback `GET /time`)
2. ISR на падающем фронте `REED_PIN` → дебаунс 100 мс → пуш в кольцевой буфер
3. Раз в секунду: дренаж буфера → `POST /ticks` с `X-API-Key`
4. При ошибке POST: тики остаются в буфере, повтор через 1 сек
5. Буфер 12 800 тиков (~52 мин оффлайн worst case)

## Настройка перед прошивкой

Открой `humstrava_firmware.ino`, замени:

| Константа | Что вписать |
|---|---|
| `WIFI_SSID` / `WIFI_PASS` | имя и пароль WiFi (только 2.4 ГГц для ESP32) |
| `API_BASE` | URL бэкенда — `http://192.168.x.x:8000` локально или Railway URL |
| `API_KEY` | тот же ключ что в `backend/.env` → `API_KEY` |

## Локальное тестирование

1. Запусти бэкенд: `cd backend && uvicorn main:app --host 0.0.0.0`
   (`--host 0.0.0.0` нужен чтобы был доступен извне localhost)
2. Узнай IP ноутбука в LAN: `ipconfig getifaddr en0` (macOS)
3. Впиши его в `API_BASE` как `http://<IP>:8000`
4. Прошей ESP32, открой Serial Monitor (115200 baud)
5. Поднеси магнит к геркону — увидишь в логах `sent N ticks`

## Зависимости (Arduino IDE)

Стандартные библиотеки ESP32 core:
- `WiFi.h`
- `WiFiClientSecure.h`
- `HTTPClient.h`
- `time.h`

Никаких внешних библиотек ставить не нужно.
