import logging

import httpx

from config import FRONTEND_URL, TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID

log = logging.getLogger(__name__)


async def send_session_start() -> None:
    if not TELEGRAM_BOT_TOKEN or not TELEGRAM_CHAT_ID:
        log.info("telegram not configured, skipping notification")
        return

    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    payload = {
        "chat_id": TELEGRAM_CHAT_ID,
        "text": f"🐹 Хомяк побежал! {FRONTEND_URL}",
    }

    # verify=False — на машине может быть HTTPS-перехват (антивирус/VPN), пускаем
    # без валидации. Уведомление содержит только публичную ссылку, без секретов.
    try:
        async with httpx.AsyncClient(timeout=5.0, verify=False) as client:
            await client.post(url, json=payload)
    except httpx.HTTPError as exc:
        log.warning("telegram send failed: %s", exc)
