#!/usr/bin/env bash
# ============================================================
# eBox 授权服务平台 - 更新脚本（在服务器上执行）
# 用法：
#   bash deploy/update.sh
# 前置：新代码已上传/同步到 APP_DIR（保留 node_modules、dist、.env 均可重新构建）
# 说明：重新安装依赖(如无变化则跳过) -> 重建前后端 -> 重启 PM2
# ============================================================
set -euo pipefail

APP_DIR="${APP_DIR:-/www/wwwroot/license-server}"
NPM_REGISTRY="${NPM_REGISTRY:-https://registry.npmmirror.com}"

echo "==> eBox 授权服务平台 更新开始"
echo "    应用目录: $APP_DIR"
[ -d "$APP_DIR/backend" ] || { echo "[错误] 未找到 $APP_DIR/backend"; exit 1; }

# ---------- 1. 后端 ----------
echo ""
echo "==> [1/3] 后端依赖 + 构建"
cd "$APP_DIR/backend"
if npm install --registry="$NPM_REGISTRY" >/dev/null 2>&1; then
  echo "    依赖安装完成"
else
  echo "    [提示] 依赖安装警告，继续构建..."
fi
npm run build
echo "    后端构建完成"

# ---------- 2. 前端 ----------
echo ""
echo "==> [2/3] 前端依赖 + 构建"
cd "$APP_DIR/admin"
if npm install --registry="$NPM_REGISTRY" >/dev/null 2>&1; then
  echo "    依赖安装完成"
else
  echo "    [提示] 依赖安装警告，继续构建..."
fi
npm run build
echo "    前端构建完成"

# ---------- 3. 重启 ----------
echo ""
echo "==> [3/3] 重启 PM2"
cd "$APP_DIR/backend"
pm2 reload license-server --update-env 2>/dev/null || pm2 restart license-server --update-env || pm2 start ecosystem.config.js --update-env
pm2 save

echo ""
echo "✅ 更新完成"
curl -sk https://abc222.cn/health || echo "（如 Nginx 尚未配置，请先部署）"
