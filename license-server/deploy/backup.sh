#!/usr/bin/env bash
# ============================================================
# eBox 授权服务平台 - 数据库备份脚本（在服务器上执行）
# 用法：
#   bash deploy/backup.sh                # 备份一次
#   crontab -e  # 每天凌晨 3 点自动备份：
#   0 3 * * * /bin/bash /www/wwwroot/license-server/deploy/backup.sh >> /www/wwwroot/license-server/deploy/backup.log 2>&1
# 说明：保留最近 30 份备份，自动清理更早的
# ============================================================
set -euo pipefail

APP_DIR="${APP_DIR:-/www/wwwroot/license-server}"
BACKUP_DIR="${BACKUP_DIR:-$APP_DIR/backups}"
KEEP="${KEEP:-30}"

ENV_FILE="$APP_DIR/backend/.env"
[ -f "$ENV_FILE" ] || { echo "[错误] 未找到 $ENV_FILE"; exit 1; }

DB_HOST="$(grep '^DB_HOST=' "$ENV_FILE" | head -n1 | cut -d= -f2- | tr -d '\r')"
DB_PORT="$(grep '^DB_PORT=' "$ENV_FILE" | head -n1 | cut -d= -f2- | tr -d '\r')"
DB_USER="$(grep '^DB_USER=' "$ENV_FILE" | head -n1 | cut -d= -f2- | tr -d '\r')"
DB_PASS="$(grep '^DB_PASSWORD=' "$ENV_FILE" | head -n1 | cut -d= -f2- | tr -d '\r')"
DB_NAME="$(grep '^DB_DATABASE=' "$ENV_FILE" | head -n1 | cut -d= -f2- | tr -d '\r')"
DB_HOST="${DB_HOST:-127.0.0.1}"
DB_PORT="${DB_PORT:-3306}"
DB_NAME="${DB_NAME:-license_server}"

mkdir -p "$BACKUP_DIR"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="$BACKUP_DIR/license_server_${STAMP}.sql.gz"

if command -v mysqldump >/dev/null 2>&1; then
  mysqldump -h"$DB_HOST" -P"$DB_PORT" -u"$DB_USER" -p"$DB_PASS" --single-transaction --routines --triggers "$DB_NAME" | gzip > "$OUT"
else
  # 宝塔 mysqldump 常在 /www/server/mysql/bin
  BT_MYSQL="/www/server/mysql/bin/mysqldump"
  [ -x "$BT_MYSQL" ] || { echo "[错误] 未找到 mysqldump"; exit 1; }
  "$BT_MYSQL" -h"$DB_HOST" -P"$DB_PORT" -u"$DB_USER" -p"$DB_PASS" --single-transaction --routines --triggers "$DB_NAME" | gzip > "$OUT"
fi

echo "✅ 备份完成: $OUT ($(du -h "$OUT" | cut -f1))"

# 清理旧备份
ls -1t "$BACKUP_DIR"/license_server_*.sql.gz 2>/dev/null | tail -n +$((KEEP + 1)) | xargs -r rm -f
echo "   已保留最近 ${KEEP} 份备份"
