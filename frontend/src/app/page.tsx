"use client";

import { useEffect, useMemo, useState } from "react";
import { Bar, BarChart, CartesianGrid, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts";
import { formatDistanceToNow } from "date-fns";
import { ru } from "date-fns/locale";

import { DEVICE_ID, supabase } from "@/lib/supabase";
import { HISTORY_DAYS, SESSION_GAP_MS, SPEED_WINDOW_MS, WHEEL_CIRCUMFERENCE_M } from "@/lib/constants";
import { currentSpeedKmh } from "@/lib/stats";
import { fetchStats, StatsResponse } from "@/lib/api";

const DAY_LABELS = ["вс", "пн", "вт", "ср", "чт", "пт", "сб"];
const INTERVAL_MS = 15 * 60 * 1000;
const REFETCH_MS = 60_000;
const RECENT_BUFFER_MS = 60_000;

export default function Dashboard() {
  const [stats, setStats] = useState<StatsResponse | null>(null);
  const [recentTicks, setRecentTicks] = useState<number[]>([]);
  const [now, setNow] = useState(() => Date.now());
  const [loading, setLoading] = useState(true);
  const [mounted, setMounted] = useState(false);

  useEffect(() => {
    setMounted(true);
    const t = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(t);
  }, []);

  useEffect(() => {
    const ctrl = new AbortController();
    const load = async () => {
      try {
        const data = await fetchStats(ctrl.signal);
        setStats(data);
        setLoading(false);
      } catch (e) {
        if ((e as Error).name !== "AbortError") console.error("fetchStats failed", e);
      }
    };
    load();
    const t = setInterval(load, REFETCH_MS);
    return () => {
      ctrl.abort();
      clearInterval(t);
    };
  }, []);

  useEffect(() => {
    const channel = supabase
      .channel("ticks-realtime")
      .on(
        "postgres_changes",
        {
          event: "INSERT",
          schema: "public",
          table: "ticks",
          filter: `device_id=eq.${DEVICE_ID}`,
        },
        (payload) => {
          const tick = Number((payload.new as { tick_ms: number }).tick_ms);
          const cutoff = Date.now() - RECENT_BUFFER_MS;
          setRecentTicks((prev) => {
            const next = prev.filter((t) => t >= cutoff);
            next.push(tick);
            return next;
          });
          setStats((prev) => applyTickToStats(prev, tick));
        },
      )
      .subscribe();
    return () => {
      supabase.removeChannel(channel);
    };
  }, []);

  const speedKmh = useMemo(() => currentSpeedKmh(recentTicks, now), [recentTicks, now]);

  const isRunningNow = useMemo(() => {
    const last = stats?.today?.lastTickMs;
    return last !== null && last !== undefined && now - last < SPEED_WINDOW_MS;
  }, [stats, now]);

  const chartData = useMemo(
    () =>
      (stats?.daily ?? []).map((d) => ({
        label: DAY_LABELS[new Date(d.dayStartMs).getDay()],
        distanceM: Math.round(d.distanceM),
      })),
    [stats?.daily],
  );

  const intervalChartData = useMemo(
    () =>
      (stats?.intervals15m ?? []).map((b) => {
        const d = new Date(b.startMs);
        return {
          label: `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`,
          distanceM: Math.round(b.distanceM),
        };
      }),
    [stats?.intervals15m],
  );

  const today = stats?.today;
  const record = stats?.record;

  return (
    <main className="min-h-screen bg-neutral-950 text-neutral-100">
      <div className="mx-auto max-w-2xl px-4 py-8 space-y-6">
        <header className="flex items-baseline justify-between">
          <h1 className="text-2xl font-bold tracking-tight">
            🐹 <span className="text-orange-500">Humstrava</span>
          </h1>
          <RunningBadge isRunning={isRunningNow} />
        </header>

        <Card>
          <Label>Скорость сейчас</Label>
          <div className="flex items-baseline gap-2">
            <span className="text-6xl font-extrabold tabular-nums">
              {speedKmh.toFixed(2)}
            </span>
            <span className="text-xl text-neutral-400">км/ч</span>
          </div>
        </Card>

        <div className="grid grid-cols-3 gap-3">
          <Stat
            label="Сегодня"
            value={formatDistance(today?.distanceM ?? 0)}
            unit={(today?.distanceM ?? 0) >= 1000 ? "км" : "м"}
          />
          <Stat label="Активно" value={formatDuration(today?.activeMs ?? 0)} unit="" />
          <Stat label="Сессий" value={String(today?.sessions ?? 0)} unit="" />
        </div>

        <Card>
          <Label>За неделю</Label>
          <div className="h-44 -mx-2">
            {mounted && (
            <ResponsiveContainer width="100%" height="100%">
              <BarChart data={chartData} margin={{ top: 10, right: 0, left: 0, bottom: 0 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="#262626" vertical={false} />
                <XAxis dataKey="label" stroke="#737373" fontSize={12} tickLine={false} axisLine={false} />
                <YAxis stroke="#737373" fontSize={12} tickLine={false} axisLine={false} width={32} />
                <Tooltip
                  cursor={{ fill: "rgba(249, 115, 22, 0.1)" }}
                  contentStyle={{ background: "#171717", border: "1px solid #404040", borderRadius: 8 }}
                  labelStyle={{ color: "#a3a3a3" }}
                  formatter={(value) => [`${value ?? 0} м`, "Дистанция"]}
                />
                <Bar dataKey="distanceM" fill="#f97316" radius={[6, 6, 0, 0]} />
              </BarChart>
            </ResponsiveContainer>
            )}
          </div>
        </Card>

        <Card>
          <Label>За 24 часа · бары по 15 мин</Label>
          <div className="h-44 -mx-2">
            {mounted && (
            <ResponsiveContainer width="100%" height="100%">
              <BarChart data={intervalChartData} margin={{ top: 10, right: 0, left: 0, bottom: 0 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="#262626" vertical={false} />
                <XAxis
                  dataKey="label"
                  stroke="#737373"
                  fontSize={11}
                  tickLine={false}
                  axisLine={false}
                  interval={15}
                />
                <YAxis stroke="#737373" fontSize={12} tickLine={false} axisLine={false} width={32} />
                <Tooltip
                  cursor={{ fill: "rgba(249, 115, 22, 0.1)" }}
                  contentStyle={{ background: "#171717", border: "1px solid #404040", borderRadius: 8 }}
                  labelStyle={{ color: "#a3a3a3" }}
                  formatter={(value) => [`${value ?? 0} м`, "Дистанция"]}
                />
                <Bar dataKey="distanceM" fill="#f97316" />
              </BarChart>
            </ResponsiveContainer>
            )}
          </div>
        </Card>

        <div className="grid grid-cols-2 gap-3">
          <Card>
            <Label>Личный рекорд</Label>
            {record && record.distanceM > 0 ? (
              <>
                <div className="text-3xl font-bold tabular-nums">
                  {formatDistance(record.distanceM)}
                  <span className="text-base text-neutral-400 ml-1">
                    {record.distanceM >= 1000 ? "км" : "м"}
                  </span>
                </div>
                <div className="text-xs text-neutral-500 mt-1">
                  {new Date(record.dayStartMs).toLocaleDateString("ru", {
                    day: "numeric",
                    month: "long",
                  })}
                </div>
              </>
            ) : (
              <div className="text-neutral-500 text-sm">пока нет данных</div>
            )}
          </Card>
          <Card>
            <Label>Последняя активность</Label>
            <div className="text-base">
              {loading ? (
                <span className="text-neutral-500">загрузка…</span>
              ) : today?.lastTickMs ? (
                isRunningNow ? (
                  <span className="text-orange-500 font-semibold">бежит прямо сейчас</span>
                ) : (
                  <span>
                    {formatDistanceToNow(today.lastTickMs, { addSuffix: true, locale: ru })}
                  </span>
                )
              ) : (
                <span className="text-neutral-500">тиков ещё не было</span>
              )}
            </div>
          </Card>
        </div>

        <footer className="text-center text-xs text-neutral-600 pt-4">
          {HISTORY_DAYS} дней · граница сессии {SESSION_GAP_MS / 60000} мин
        </footer>
      </div>
    </main>
  );
}

function applyTickToStats(prev: StatsResponse | null, tick: number): StatsResponse | null {
  if (!prev) return prev;

  let today = prev.today;
  if (today && tick >= today.dayStartMs) {
    today = {
      ...today,
      ticks: today.ticks + 1,
      distanceM: today.distanceM + WHEEL_CIRCUMFERENCE_M,
      lastTickMs: tick,
    };
  }

  let daily = prev.daily;
  if (daily.length > 0) {
    const last = daily[daily.length - 1];
    if (tick >= last.dayStartMs && tick < last.dayStartMs + 24 * 60 * 60 * 1000) {
      daily = [
        ...daily.slice(0, -1),
        { ...last, ticks: last.ticks + 1, distanceM: last.distanceM + WHEEL_CIRCUMFERENCE_M },
      ];
    }
  }

  let intervals = prev.intervals15m;
  if (intervals.length > 0) {
    const lastBucket = intervals[intervals.length - 1];
    const windowStart = intervals[0].startMs;
    const windowEnd = lastBucket.startMs + INTERVAL_MS;
    if (tick >= windowStart && tick < windowEnd) {
      const idx = Math.floor((tick - windowStart) / INTERVAL_MS);
      const bucket = intervals[idx];
      intervals = [
        ...intervals.slice(0, idx),
        { ...bucket, ticks: bucket.ticks + 1, distanceM: bucket.distanceM + WHEEL_CIRCUMFERENCE_M },
        ...intervals.slice(idx + 1),
      ];
    }
  }

  return { ...prev, today, daily, intervals15m: intervals };
}

function Card({ children }: { children: React.ReactNode }) {
  return (
    <div className="rounded-2xl bg-neutral-900 border border-neutral-800 p-5">{children}</div>
  );
}

function Label({ children }: { children: React.ReactNode }) {
  return (
    <div className="text-xs uppercase tracking-widest text-neutral-500 mb-2">{children}</div>
  );
}

function Stat({ label, value, unit }: { label: string; value: string; unit: string }) {
  return (
    <Card>
      <Label>{label}</Label>
      <div className="flex items-baseline gap-1">
        <span className="text-2xl font-bold tabular-nums">{value}</span>
        {unit && <span className="text-sm text-neutral-400">{unit}</span>}
      </div>
    </Card>
  );
}

function RunningBadge({ isRunning }: { isRunning: boolean }) {
  if (!isRunning) return null;
  return (
    <div className="flex items-center gap-2 text-sm">
      <span className="relative flex h-2 w-2">
        <span className="absolute inline-flex h-full w-full animate-ping rounded-full bg-orange-500 opacity-75" />
        <span className="relative inline-flex h-2 w-2 rounded-full bg-orange-500" />
      </span>
      <span className="text-orange-500 font-medium">live</span>
    </div>
  );
}

function formatDistance(m: number): string {
  if (m >= 1000) return (m / 1000).toFixed(2);
  return Math.round(m).toString();
}

function formatDuration(ms: number): string {
  const totalMin = Math.floor(ms / 60000);
  const hours = Math.floor(totalMin / 60);
  const mins = totalMin % 60;
  if (hours > 0) return `${hours}ч ${mins}м`;
  return `${mins}м`;
}
