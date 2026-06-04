import os
from dotenv import load_dotenv

load_dotenv()

DATABASE_URL = os.environ["DATABASE_URL"]
API_KEY = os.environ["API_KEY"]

TELEGRAM_BOT_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN", "")
TELEGRAM_CHAT_ID = os.getenv("TELEGRAM_CHAT_ID", "")
FRONTEND_URL = os.getenv("FRONTEND_URL", "https://humstrava.vercel.app")

SESSION_GAP_MS = int(os.getenv("SESSION_GAP_MS", "180000"))
