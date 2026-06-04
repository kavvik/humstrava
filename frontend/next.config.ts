import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Разрешаем dev-сервер с LAN IP, иначе HMR WebSocket рушит React-бутстрап
  // и useEffect не выполняется, фронт никогда не делает запрос к Supabase.
  allowedDevOrigins: ["192.168.1.129", "*.local"],
};

export default nextConfig;
