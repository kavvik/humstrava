---
name: humstrava-regress
description: Регресс-чек Humstrava — поднимает backend/frontend, прогоняет API+UI сценарии через chrome-devtools MCP, проверяет console/network/snapshot. Запускать после изменений в backend/ или frontend/src/ (хук на PostToolUse напоминает автоматически).
---

# Регресс-чек Humstrava

Этот скилл прогоняет регрессионные сценарии после изменений в backend/ или frontend/src/.
Хук `.claude/hooks/regress-reminder.sh` напоминает запустить скилл когда нужно.

## Когда запускать

- После завершения логической партии изменений в `backend/*.py` или `frontend/src/**`
- НЕ запускать после каждой микро-правки одной фичи — дождаться когда фича в текущем туре собралась
- Запускать ДО финального ответа пользователю, чтобы доложить результат вместе с диффом

## Подготовка окружения

### 1. Проверить что dev-сервера подняты

```bash
# Backend
curl -sf http://localhost:8000/health > /dev/null && echo backend_up || echo backend_down

# Frontend
curl -sf http://localhost:3000 > /dev/null && echo frontend_up || echo frontend_down
```

Если что-то лежит — поднять в фоне через `Bash run_in_background=true`:

```bash
# Backend
cd /Users/a.koziukin/Documents/code/Humstrava/backend && \
  uvicorn main:app --reload --host 0.0.0.0 --port 8000 > /tmp/humstrava-backend.log 2>&1

# Frontend
cd /Users/a.koziukin/Documents/code/Humstrava/frontend && \
  npm run dev > /tmp/humstrava-frontend.log 2>&1
```

Дождаться готовности через Bash `run_in_background=true` + одноразовый `until` (НЕ Monitor — нужна одна нотификация):

```bash
until curl -sf http://localhost:8000/health > /dev/null; do sleep 0.5; done
until curl -sf http://localhost:3000 > /dev/null; do sleep 0.5; done
```

## Backend регресс-сценарии

Прогнать через curl. Каждый сценарий — отдельный шаг, фиксировать pass/fail.

### B1. /health
```bash
curl -s http://localhost:8000/health
```
Ожидание: `{"ok":true}`

### B2. /openapi.json содержит все маршруты
```bash
curl -s http://localhost:8000/openapi.json | jq -r '.paths | keys[]' | sort
```
Ожидание: `/health`, `/stats`, `/ticks`, `/time` — все 4

### B3. /stats — структура ответа
```bash
curl -s "http://localhost:8000/stats?device_id=hamster-wheel&days=7&tz=Europe/Madrid" | jq '{
  has_now: (.now | type == "number"),
  has_today: (.today != null),
  today_keys: (.today | keys | sort),
  daily_len: (.daily | length),
  intervals_len: (.intervals15m | length),
  record_or_null: (.record == null or (.record | has("distanceM")))
}'
```
Ожидание:
- `has_now: true`
- `today_keys: ["activeMs","dayStartMs","distanceM","lastTickMs","sessions","ticks"]`
- `daily_len: 7`
- `intervals_len: 96`
- `record_or_null: true`

### B4. /stats — числовая валидность
```bash
curl -s "http://localhost:8000/stats?device_id=hamster-wheel&days=7&tz=Europe/Madrid" | jq '{
  today_distance_nonneg: (.today.distanceM >= 0),
  today_ticks_nonneg: (.today.ticks >= 0),
  daily_all_nonneg: ([.daily[].distanceM >= 0] | all),
  intervals_all_nonneg: ([.intervals15m[].distanceM >= 0] | all),
  daily_last_is_today: (.daily[-1].dayStartMs == .today.dayStartMs)
}'
```
Ожидание: все `true`.

### B5. /ticks без X-API-Key отвергается
```bash
curl -s -o /dev/null -w "%{http_code}\n" -X POST http://localhost:8000/ticks \
  -H "Content-Type: application/json" \
  -d '{"d":"hamster-wheel","ticks":[]}'
```
Ожидание: `401` (наша проверка) или `422` (FastAPI валидация обязательного заголовка). Главное — НЕ `200`.

### B6. CORS preflight для /stats
```bash
curl -s -o /dev/null -w "%{http_code}\n" -X OPTIONS http://localhost:8000/stats \
  -H "Origin: http://localhost:3000" \
  -H "Access-Control-Request-Method: GET"
```
Ожидание: `200` или `204`

## Frontend регресс-сценарии (через chrome-devtools MCP)

Использовать инструменты `mcp__chrome-devtools__*`. ВАЖНО: они доступны только если MCP подключен в текущей сессии — `claude mcp list` должен показать `chrome-devtools: ✓ Connected`.

### F1. Открыть страницу
```
mcp__chrome-devtools__new_page url="http://localhost:3000"
# или, если page уже открыта:
mcp__chrome-devtools__navigate_page url="http://localhost:3000"
```

### F2. Подождать рендера
```
mcp__chrome-devtools__wait_for text="Humstrava"
```

### F3. Console — нет errors
```
mcp__chrome-devtools__list_console_messages
```
Ожидание: НЕТ сообщений уровня `error`. Допустимо: `warning` от Recharts/HMR, `info` от Supabase. Если есть `error` — это регресс (особенно "hydration mismatch", "Failed to fetch", "supabase").

### F4. Network — /stats отвечает 200
```
mcp__chrome-devtools__list_network_requests resourceTypes=["fetch","xhr"]
```
Найти запрос с URL содержащим `/stats?device_id=` → статус `200`. Если нет такого запроса вообще → регресс (фронт не дёрнул API).

### F5. Snapshot — все секции на месте
```
mcp__chrome-devtools__take_snapshot
```
В accessibility tree должны присутствовать тексты:
- `Humstrava` (заголовок)
- `Скорость сейчас`
- `Сегодня`
- `Активно`
- `Сессий`
- `За неделю`
- `За 24 часа · бары по 15 мин`
- `Личный рекорд`
- `Последняя активность`

Если чего-то нет → регресс (фронт упал или поломан рендер).

### F6. Screenshot для визуальной фиксации
```
mcp__chrome-devtools__take_screenshot
```
Приложить к ответу пользователю как доказательство.

## Сценарий end-to-end (опционально, если правки касались ingestion / realtime)

### E1. POST тика и проверка инкремента today.ticks
```bash
NOW_MS=$(python3 -c "import time; print(int(time.time()*1000))")
BEFORE=$(curl -s "http://localhost:8000/stats?device_id=hamster-wheel" | jq '.today.ticks')

curl -s -X POST http://localhost:8000/ticks \
  -H "X-API-Key: $(grep ^API_KEY /Users/a.koziukin/Documents/code/Humstrava/backend/.env | cut -d= -f2)" \
  -H "Content-Type: application/json" \
  -d "{\"d\":\"hamster-wheel\",\"ticks\":[$NOW_MS]}"

sleep 1
AFTER=$(curl -s "http://localhost:8000/stats?device_id=hamster-wheel" | jq '.today.ticks')
echo "before=$BEFORE after=$AFTER"
```
Ожидание: `after == before + 1`

ВНИМАНИЕ: меняет состояние БД. Запускать только если правки касались `/ticks`, `db.insert_ticks`, или realtime-логики.

## Формат отчёта

После прогона выдать сводку:

```
✅ Backend: B1 B2 B3 B4 B5 B6 — все pass
✅ Frontend: F1 F2 F3 F4 F5 F6 — все pass

или

⚠️ Backend: B1 B2 B5 B6 pass | B3 fail (today_keys пропущен activeMs) | B4 skip
❌ Frontend: F1 F2 pass | F3 fail (1 console.error: "ERR_CONNECTION_REFUSED /stats") | F4-F6 skip
```

Скриншот F6 приложить если был сделан.

## Что НЕ делать

- Не запускать линт/тесты как часть регресса — это `npm run build` / `tsc --noEmit`, они отдельно
- Не убивать чужие процессы на 8000/3000 без подтверждения у пользователя
- Не очищать БД, не запускать миграции
- Не коммитить, не пушить
