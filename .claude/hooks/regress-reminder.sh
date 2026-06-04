#!/usr/bin/env bash
# PostToolUse hook: при правках в backend/ или frontend/src/ напомнить Клоду
# запустить /humstrava-regress перед концом текущего хода.
#
# Получает на stdin JSON с информацией о tool call.
# Если file_path попадает в зону регресса — пишет в stdout system-reminder.

set -e

input=$(cat)
file_path=$(echo "$input" | /usr/bin/env jq -r '.tool_input.file_path // empty' 2>/dev/null || true)

# Пусто — другой тип tool, или нет file_path
[ -z "$file_path" ] && exit 0

case "$file_path" in
  */Humstrava/backend/*.py | \
  */Humstrava/backend/schema.sql | \
  */Humstrava/frontend/src/* | \
  */Humstrava/frontend/next.config.* | \
  */Humstrava/frontend/package.json )
    cat <<'EOF'
<system-reminder>
В файлах backend/ или frontend/src/ Humstrava были изменения. Перед завершением текущего хода запусти регресс через Skill tool с skill="humstrava-regress" — он проверит API, UI, console и network через chrome-devtools MCP. Не запускай после каждой микро-правки одной фичи: дождись когда логическая партия изменений в туре закрыта.
</system-reminder>
EOF
    ;;
esac

exit 0
