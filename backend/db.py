import asyncpg

from config import DATABASE_URL

_pool: asyncpg.Pool | None = None


async def init_pool() -> None:
    global _pool
    _pool = await asyncpg.create_pool(DATABASE_URL, min_size=1, max_size=5)


async def close_pool() -> None:
    if _pool is not None:
        await _pool.close()


async def insert_ticks(device_id: str, tick_ms_list: list[int]) -> int:
    assert _pool is not None, "pool not initialized"
    async with _pool.acquire() as conn:
        rows = await conn.fetch(
            """
            INSERT INTO ticks (device_id, tick_ms)
            SELECT $1, unnest($2::bigint[])
            ON CONFLICT (device_id, tick_ms) DO NOTHING
            RETURNING tick_ms
            """,
            device_id,
            tick_ms_list,
        )
        return len(rows)


async def get_stats(
    device_id: str,
    days: int,
    tz: str,
    intervals: int,
    interval_ms: int,
    gap_ms: int,
) -> dict:
    assert _pool is not None, "pool not initialized"
    async with _pool.acquire() as conn:
        today_row = await conn.fetchrow(
            """
            WITH today_start AS (
                SELECT (EXTRACT(EPOCH FROM (date_trunc('day', now() AT TIME ZONE $2) AT TIME ZONE $2)) * 1000)::bigint AS ms
            ),
            today_ticks AS (
                SELECT t.tick_ms,
                       LAG(t.tick_ms) OVER (ORDER BY t.tick_ms) AS prev_ms
                FROM ticks t, today_start
                WHERE t.device_id = $1 AND t.tick_ms >= today_start.ms
            )
            SELECT
                (SELECT ms FROM today_start) AS day_start_ms,
                COUNT(*)::bigint AS ticks,
                MAX(tick_ms) AS last_tick_ms,
                COUNT(*) FILTER (WHERE prev_ms IS NULL OR (tick_ms - prev_ms) > $3)::bigint AS sessions,
                COALESCE(SUM(tick_ms - prev_ms) FILTER (WHERE prev_ms IS NOT NULL AND (tick_ms - prev_ms) <= $3), 0)::bigint AS active_ms
            FROM today_ticks
            """,
            device_id,
            tz,
            gap_ms,
        )

        daily_rows = await conn.fetch(
            """
            WITH days AS (
                SELECT generate_series(
                    date_trunc('day', now() AT TIME ZONE $2) - (($3 - 1) * INTERVAL '1 day'),
                    date_trunc('day', now() AT TIME ZONE $2),
                    INTERVAL '1 day'
                ) AS day_local
            )
            SELECT
                (EXTRACT(EPOCH FROM (d.day_local AT TIME ZONE $2)) * 1000)::bigint AS day_start_ms,
                COUNT(t.tick_ms)::bigint AS ticks
            FROM days d
            LEFT JOIN ticks t
                ON t.device_id = $1
                AND t.tick_ms >= (EXTRACT(EPOCH FROM (d.day_local AT TIME ZONE $2)) * 1000)::bigint
                AND t.tick_ms <  (EXTRACT(EPOCH FROM ((d.day_local + INTERVAL '1 day') AT TIME ZONE $2)) * 1000)::bigint
            GROUP BY d.day_local
            ORDER BY d.day_local
            """,
            device_id,
            tz,
            days,
        )

        interval_rows = await conn.fetch(
            """
            WITH bounds AS (
                SELECT
                    (EXTRACT(EPOCH FROM now()) * 1000)::bigint AS now_ms,
                    $2::int AS bucket_count,
                    $3::bigint AS bucket_ms
            ),
            buckets AS (
                SELECT (b.now_ms - (b.bucket_count - i) * b.bucket_ms) AS start_ms,
                       b.bucket_ms
                FROM bounds b, generate_series(1, b.bucket_count) AS i
            )
            SELECT
                buckets.start_ms,
                COUNT(t.tick_ms)::bigint AS ticks
            FROM buckets
            LEFT JOIN ticks t
                ON t.device_id = $1
                AND t.tick_ms >= buckets.start_ms
                AND t.tick_ms <  buckets.start_ms + buckets.bucket_ms
            GROUP BY buckets.start_ms
            ORDER BY buckets.start_ms
            """,
            device_id,
            intervals,
            interval_ms,
        )

        record_row = await conn.fetchrow(
            """
            WITH per_day AS (
                SELECT date_trunc('day', to_timestamp(tick_ms / 1000.0) AT TIME ZONE $2) AS day_local,
                       COUNT(*)::bigint AS ticks
                FROM ticks
                WHERE device_id = $1
                GROUP BY 1
            )
            SELECT
                (EXTRACT(EPOCH FROM (day_local AT TIME ZONE $2)) * 1000)::bigint AS day_start_ms,
                ticks
            FROM per_day
            ORDER BY ticks DESC
            LIMIT 1
            """,
            device_id,
            tz,
        )

        return {
            "today": dict(today_row) if today_row else None,
            "daily": [dict(r) for r in daily_rows],
            "intervals": [dict(r) for r in interval_rows],
            "record": dict(record_row) if record_row else None,
        }
