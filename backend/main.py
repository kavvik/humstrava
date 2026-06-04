import time
from contextlib import asynccontextmanager

from fastapi import Depends, FastAPI, Header, HTTPException, Query, status
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

import db
import telegram
from config import API_KEY, SESSION_GAP_MS

WHEEL_CIRCUMFERENCE_M = 0.6597

_last_tick_ms: dict[str, int] = {}


@asynccontextmanager
async def lifespan(app: FastAPI):
    await db.init_pool()
    try:
        yield
    finally:
        await db.close_pool()


app = FastAPI(title="Humstrava API", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["GET", "POST"],
    allow_headers=["*"],
)


def require_api_key(x_api_key: str = Header(..., alias="X-API-Key")) -> None:
    if x_api_key != API_KEY:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid api key")


class TickBatch(BaseModel):
    d: str = Field(..., min_length=1, max_length=64, description="device_id")
    ticks: list[int] = Field(..., description="Unix timestamps in milliseconds")


@app.post("/ticks", dependencies=[Depends(require_api_key)])
async def ingest_ticks(batch: TickBatch) -> dict:
    if not batch.ticks:
        return {"inserted": 0, "new_session": False}

    first_tick = min(batch.ticks)
    last_tick = max(batch.ticks)

    prev = _last_tick_ms.get(batch.d)
    is_new_session = prev is None or (first_tick - prev) > SESSION_GAP_MS

    _last_tick_ms[batch.d] = last_tick

    inserted = await db.insert_ticks(batch.d, batch.ticks)

    if is_new_session:
        await telegram.send_session_start()

    return {"inserted": inserted, "new_session": is_new_session}


@app.get("/time")
async def get_time() -> dict:
    return {"ms": int(time.time() * 1000)}


@app.get("/health")
async def health() -> dict:
    return {"ok": True}


@app.get("/stats")
async def get_stats(
    device_id: str = Query(...),
    days: int = Query(7, ge=1, le=90),
    tz: str = Query("Europe/Moscow"),
    intervals: int = Query(96, ge=1, le=288),
    interval_ms: int = Query(15 * 60 * 1000, ge=60_000),
    gap_ms: int = Query(SESSION_GAP_MS, ge=1000),
) -> dict:
    raw = await db.get_stats(
        device_id=device_id,
        days=days,
        tz=tz,
        intervals=intervals,
        interval_ms=interval_ms,
        gap_ms=gap_ms,
    )

    today = raw["today"]
    today_out = {
        "dayStartMs": today["day_start_ms"],
        "ticks": today["ticks"],
        "distanceM": round(today["ticks"] * WHEEL_CIRCUMFERENCE_M, 3),
        "lastTickMs": today["last_tick_ms"],
        "sessions": today["sessions"],
        "activeMs": today["active_ms"],
    } if today else None

    daily_out = [
        {
            "dayStartMs": r["day_start_ms"],
            "ticks": r["ticks"],
            "distanceM": round(r["ticks"] * WHEEL_CIRCUMFERENCE_M, 3),
        }
        for r in raw["daily"]
    ]

    intervals_out = [
        {
            "startMs": r["start_ms"],
            "ticks": r["ticks"],
            "distanceM": round(r["ticks"] * WHEEL_CIRCUMFERENCE_M, 3),
        }
        for r in raw["intervals"]
    ]

    record = raw["record"]
    record_out = {
        "dayStartMs": record["day_start_ms"],
        "ticks": record["ticks"],
        "distanceM": round(record["ticks"] * WHEEL_CIRCUMFERENCE_M, 3),
    } if record else None

    return {
        "now": int(time.time() * 1000),
        "today": today_out,
        "daily": daily_out,
        "intervals15m": intervals_out,
        "record": record_out,
    }
