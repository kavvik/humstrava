import { SPEED_WINDOW_MS, WHEEL_CIRCUMFERENCE_M } from "./constants";

export function distanceMeters(tickCount: number): number {
  return tickCount * WHEEL_CIRCUMFERENCE_M;
}

/**
 * Скорость по последним SPEED_WINDOW_MS миллисекундам.
 * Возвращает 0, если в окне меньше 2 тиков (нечем посчитать интервал).
 */
export function currentSpeedKmh(sortedTicks: number[], now: number): number {
  const windowStart = now - SPEED_WINDOW_MS;
  let i = sortedTicks.length - 1;
  const recent: number[] = [];
  while (i >= 0 && sortedTicks[i] >= windowStart) {
    recent.unshift(sortedTicks[i]);
    i--;
  }
  if (recent.length < 2) return 0;
  const spanSec = (recent[recent.length - 1] - recent[0]) / 1000;
  if (spanSec <= 0) return 0;
  return ((recent.length - 1) * WHEEL_CIRCUMFERENCE_M * 3.6) / spanSec;
}

/** Начало текущего локального дня в unix ms. */
export function startOfLocalDay(date: Date = new Date()): number {
  const d = new Date(date);
  d.setHours(0, 0, 0, 0);
  return d.getTime();
}
