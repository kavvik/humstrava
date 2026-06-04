# Humstrava — Frontend

Дашборд хомячьего колеса. Next.js 16 (App Router) + React 19 + Tailwind v4 + Supabase Realtime + Recharts.

## Что показывает

- **Скорость сейчас** — обновляется в реальном времени по последним 3 секундам тиков
- **Сегодня** — дистанция, активное время, количество сессий
- **За неделю** — столбчатый график дистанции по дням (в локальном timezone браузера, OQ-017)
- **Личный рекорд** — лучший день за последнюю неделю
- **Последняя активность** — "X минут назад" или "бежит сейчас"

## Архитектура данных

1. При загрузке: один SELECT за последние 7 дней по `device_id`
2. Подписка на Supabase Realtime (postgres_changes / INSERT) — новые тики приходят без polling'а
3. Все агрегаты считаются на клиенте из массива `tick_ms`

Backend для запросов не нужен — RLS на `ticks` разрешает anon read.

## Локальный запуск

1. Скопируй `.env.example` → `.env.local`, заполни:
   - `NEXT_PUBLIC_SUPABASE_URL` — Project Settings → API → Project URL
   - `NEXT_PUBLIC_SUPABASE_ANON_KEY` — Project Settings → API → `anon public`
   - `NEXT_PUBLIC_DEVICE_ID` — должен совпадать с `DEVICE_ID` в прошивке ESP32 (`hamster-wheel`)

2. Запусти dev-сервер:
   ```bash
   npm run dev
   ```

3. Открой http://localhost:3000

## Структура

```
src/
├── app/
│   ├── layout.tsx       — root layout (lang="ru", метаданные)
│   ├── page.tsx         — дашборд (client component, всё в одном файле для MVP)
│   └── globals.css      — Tailwind v4
└── lib/
    ├── supabase.ts      — клиент + DEVICE_ID
    ├── constants.ts     — диаметр колеса, граница сессии, окно скорости
    └── stats.ts         — distance, sessions, currentSpeedKmh, dailyStats
```

## Деплой на Vercel

```bash
npx vercel
```

В дашборде Vercel прописать те же переменные окружения, что в `.env.local`.
