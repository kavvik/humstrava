-- Humstrava — схема БД
-- Выполнить один раз в Supabase SQL Editor.

CREATE TABLE IF NOT EXISTS ticks (
  id          BIGSERIAL PRIMARY KEY,
  device_id   TEXT NOT NULL,
  tick_ms     BIGINT NOT NULL,
  created_at  TIMESTAMPTZ DEFAULT NOW(),
  CONSTRAINT unique_tick UNIQUE (device_id, tick_ms)
);

-- Включить Supabase Realtime для таблицы.
-- Фронт подписывается на INSERT и получает события через WebSocket.
ALTER PUBLICATION supabase_realtime ADD TABLE ticks;

-- Row Level Security: backend пишет через session pooler как postgres user
-- (обходит RLS), фронт читает через anon key (RLS применяется).
ALTER TABLE ticks ENABLE ROW LEVEL SECURITY;

-- Публичное чтение для MVP (один хомяк, данные не критичные).
-- При переходе на multi-tenant заменить на policy с проверкой user_id.
CREATE POLICY "allow anon read"
  ON ticks FOR SELECT
  TO anon
  USING (true);
