// Диаметр беговой дорожки колеса: 21 см.
// Длина окружности = π × 0.21 = 0.6597 м (см. docs/decisions.md).
export const WHEEL_CIRCUMFERENCE_M = 0.6597;

// Порог границы сессии: 3 мин (OQ-018). Можно сделать настраиваемым в UI.
export const SESSION_GAP_MS = 3 * 60 * 1000;

// Окно для расчёта текущей скорости (real-time).
export const SPEED_WINDOW_MS = 3000;

// Сколько дней истории грузим при заходе на дашборд.
export const HISTORY_DAYS = 7;
