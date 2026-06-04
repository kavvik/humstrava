import { DEVICE_ID } from "./supabase";
import { HISTORY_DAYS, SESSION_GAP_MS } from "./constants";

const API_URL = process.env.NEXT_PUBLIC_API_URL ?? "http://localhost:8000";

export type TodayStats = {
  dayStartMs: number;
  ticks: number;
  distanceM: number;
  lastTickMs: number | null;
  sessions: number;
  activeMs: number;
};

export type DayStat = {
  dayStartMs: number;
  ticks: number;
  distanceM: number;
};

export type IntervalStat = {
  startMs: number;
  ticks: number;
  distanceM: number;
};

export type RecordDay = {
  dayStartMs: number;
  ticks: number;
  distanceM: number;
};

export type StatsResponse = {
  now: number;
  today: TodayStats | null;
  daily: DayStat[];
  intervals15m: IntervalStat[];
  record: RecordDay | null;
};

export async function fetchStats(signal?: AbortSignal): Promise<StatsResponse> {
  const tz = Intl.DateTimeFormat().resolvedOptions().timeZone || "Europe/Moscow";
  const params = new URLSearchParams({
    device_id: DEVICE_ID,
    days: String(HISTORY_DAYS),
    tz,
    intervals: "96",
    interval_ms: String(15 * 60 * 1000),
    gap_ms: String(SESSION_GAP_MS),
  });
  const res = await fetch(`${API_URL}/stats?${params}`, { signal, cache: "no-store" });
  if (!res.ok) throw new Error(`/stats ${res.status}`);
  return res.json();
}
