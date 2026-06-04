# Humstrava — Backend

FastAPI-сервис на Railway. Принимает тики от ESP32, пишет в Supabase, шлёт Telegram при старте сессии.

## Endpoints

| Метод | Путь | Назначение |
|---|---|---|
| `POST` | `/ticks` | ESP32 шлёт батч тиков. Требует `X-API-Key`. |
| `GET` | `/time` | NTP fallback для ESP32 — возвращает UTC ms. |
| `GET` | `/health` | Health check. |

## Локальный запуск

```bash
cd backend
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env  # отредактировать значения
uvicorn main:app --reload
```

## Перед первым запуском

1. Создать проект в Supabase, выполнить `schema.sql` в SQL Editor.
2. Скопировать connection string в `.env` → `DATABASE_URL`.
3. Сгенерировать `API_KEY` (например `python -c "import secrets; print(secrets.token_urlsafe(32))"`).
4. Опционально: создать Telegram-бота через @BotFather, узнать `chat_id`, заполнить в `.env`.

## Деплой на Railway

Railway автодетектирует Python-проект по `requirements.txt`.
Команда запуска: `uvicorn main:app --host 0.0.0.0 --port $PORT`
Переменные окружения — те же, что в `.env`.
