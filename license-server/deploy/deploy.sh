#!/usr/bin/env bash
# ============================================================
# eBox 授权服务平台 - 首次部署脚本（宝塔/Linux，在服务器上执行）
# 用法（在服务器终端）：
#   chmod +x deploy/deploy.sh
#   bash deploy/deploy.sh
# 可选环境变量：
#   DOMAIN=abc222.cn        部署域名
#   APP_DIR=/www/wwwroot/license-server   应用根目录（代码所在处）
#   ADMIN_USERNAME=admin    管理员账号
#   ADMIN_PASSWORD=xxx      管理员密码（默认 admin123，务必改）
#   NPM_REGISTRY=https://registry.npmmirror.com    npm 镜像
# 说明：会执行 依赖安装 -> 构建 -> 生成 .env -> 初始化数据库/管理员 -> PM2 启动
# ============================================================
set -euo pipefail

DOMAIN="${DOMAIN:-abc222.cn}"
APP_DIR="${APP_DIR:-/www/wwwroot/license-server}"
ADMIN_USERNAME="${ADMIN_USERNAME:-admin}"
ADMIN_PASSWORD="${ADMIN_PASSWORD:-admin123}"
NPM_REGISTRY="${NPM_REGISTRY:-https://registry.npmmirror.com}"

echo "==> eBox 授权服务平台 首次部署开始"
echo "    域名: $DOMAIN"
echo "    应用目录: $APP_DIR"

# ---------- 1. 环境检查 ----------
if ! command -v node >/dev/null 2>&1; then
  echo "[错误] 未找到 node。请先在宝塔安装 Node.js 版本管理器并启用 >=22 版本"
  exit 1
fi
if ! command -v npm >/dev/null 2>&1; then
  echo "[错误] 未找到 npm"
  exit 1
fi
if ! command -v pm2 >/dev/null 2>&1; then
  echo "[提示] 未安装 pm2，正在安装..."
  npm install -g pm2 --registry="$NPM_REGISTRY"
fi
NODE_MAJOR="$(node -v | sed 's/v\([0-9]*\).*/\1/')"
if [ "$NODE_MAJOR" -lt 22 ]; then
  echo "[错误] 需要 Node >= 22（当前 $(node -v)）。请用宝塔 Node 版本管理器切换到 22.x"
  exit 1
fi
echo "    Node $(node -v) / npm $(npm -v)"

# ---------- 2. 代码就位检查 ----------
if [ ! -d "$APP_DIR/backend" ] || [ ! -d "$APP_DIR/admin" ]; then
  echo "[错误] 未在 $APP_DIR 下找到 backend/admin 目录。"
  echo "        请先在宝塔建站 /www/wwwroot/license-server，然后把代码整包上传到该目录（保留 package-lock.json）。"
  echo "        本机可执行：rsync -av --exclude node_modules --exclude dist ./license-server/ root@服务器IP:$APP_DIR/"
  exit 1
fi
cd "$APP_DIR"

# ---------- 3. 后端：依赖 + 构建 ----------
echo ""
echo "==> [1/5] 后端依赖安装与构建"
cd "$APP_DIR/backend"
if [ ! -f package-lock.json ]; then
  echo "[错误] backend 缺少 package-lock.json，请整包上传（不要只传 src）"
  exit 1
fi
if npm ci --registry="$NPM_REGISTRY"; then :; else
  echo "[提示] npm ci 失败，改用 npm install"
  npm install --registry="$NPM_REGISTRY"
fi
npm run build
echo "    后端构建完成: dist/app.js"

# ---------- 4. 前端：依赖 + 构建 ----------
echo ""
echo "==> [2/5] 前端依赖安装与构建"
cd "$APP_DIR/admin"
if [ ! -f package-lock.json ]; then
  echo "[错误] admin 缺少 package-lock.json"
  exit 1
fi
if npm ci --registry="$NPM_REGISTRY"; then :; else
  echo "[提示] npm ci 失败，改用 npm install"
  npm install --registry="$NPM_REGISTRY"
fi
npm run build
echo "    前端构建完成: admin/dist"

# ---------- 5. 环境配置 .env ----------
echo ""
echo "==> [3/5] 环境配置 (.env)"
cd "$APP_DIR/backend"
if [ ! -f .env ]; then
  cp .env.example .env
  echo "    已从 .env.example 生成 .env"
  echo "    【重要】请立即编辑 .env："
  echo "      - DB_PASSWORD：MySQL 密码"
  echo "      - JWT_SECRET：随机长字符串"
  echo "      - LICENSE_PRIVATE_KEY_D：保留默认出厂密钥即可（与客户端公钥配套）"
else
  echo "    .env 已存在，跳过生成"
fi

# ---------- 6. 数据库初始化 ----------
echo ""
echo "==> [4/5] 数据库初始化"
DB_USER="$(grep '^DB_USER=' "$APP_DIR/backend/.env" | head -n1 | cut -d= -f2- | tr -d '\r' || true)"
DB_PASS="$(grep '^DB_PASSWORD=' "$APP_DIR/backend/.env" | head -n1 | cut -d= -f2- | tr -d '\r' || true)"
DB_NAME="$(grep '^DB_DATABASE=' "$APP_DIR/backend/.env" | head -n1 | cut -d= -f2- | tr -d '\r' || true)"
DB_NAME="${DB_NAME:-license_server}"
DB_USER="${DB_USER:-license}"

cd "$APP_DIR/backend"
if command -v mysql >/dev/null 2>&1; then
  if [ -z "$DB_PASS" ] || [ "$DB_PASS" = "请修改为强密码" ]; then
    echo "    [跳过] .env 中 DB_PASSWORD 未配置，请在编辑 .env 后手动执行："
    echo "           mysql -u${DB_USER} -p < $APP_DIR/database/schema.sql"
    echo "           或直接到宝塔 -> 数据库 -> 导入 $APP_DIR/database/schema.sql"
  else
    # 检查是否已导入过（license_keys 表是否存在）
    TABLE_CNT="$(mysql -u"$DB_USER" -p"$DB_PASS" -h127.0.0.1 -N -e \
      "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='${DB_NAME}' AND table_name='license_keys';" 2>/dev/null || echo "ERR")"
    if [ "$TABLE_CNT" = "ERR" ] || [ -z "$TABLE_CNT" ]; then
      echo "    [跳过] 无法用 ${DB_USER} 连接 MySQL（可能在 .env 里密码还没改）。"
      echo "           请先编辑 .env 填写正确的 DB_PASSWORD，再手动导入 schema.sql："
      echo "           mysql -u${DB_USER} -p${DB_PASS} < $APP_DIR/database/schema.sql"
    elif [ "$TABLE_CNT" -ge 1 ]; then
      echo "    数据库 ${DB_NAME} 已初始化，跳过导入"
    else
      echo "    导入表结构: schema.sql"
      mysql -u"$DB_USER" -p"$DB_PASS" -h127.0.0.1 < "$APP_DIR/database/schema.sql" && echo "    导入成功"
    fi
  fi
else
  echo "    [提示] 服务器无 mysql 命令行。请用宝塔面板 -> 数据库 创建 ${DB_NAME} 并导入："
  echo "           $APP_DIR/database/schema.sql"
fi

echo ""
echo "    执行数据库连通校验 + 初始化管理员..."
if npm run init:db; then :; else
  echo "    [警告] 数据库校验失败。请检查 .env 的 DB_* 配置与 MySQL 状态后重新执行本脚本（构建步骤会跳过）。"
fi
npm run init:admin -- --username "$ADMIN_USERNAME" --password "$ADMIN_PASSWORD"

# ---------- 7. PM2 启动 ----------
echo ""
echo "==> [5/5] PM2 启动"
cd "$APP_DIR/backend"
if [ -d logs ]; then :; else mkdir -p logs; fi
pm2 start ecosystem.config.js --update-env
pm2 save

# ---------- 8. 完成提示 ----------
echo ""
echo "============================================================"
echo " 部署完成！接下来请完成："
echo ""
echo " 1) Nginx 站点配置（域名 $DOMAIN）"
echo "    复制 deploy/nginx/abc222.cn.conf 到站点配置，或按文档核对"
echo " 2) SSL 证书"
echo "    宝塔 -> 网站 -> $DOMAIN -> SSL -> Let's Encrypt 一键申请"
echo " 3) 验证"
echo "    curl -k https://$DOMAIN/health"
echo "    浏览器打开 https://$DOMAIN 登录管理面板"
echo "    管理员账号: $ADMIN_USERNAME"
echo ""
echo " 客户端说明：eBox 默认连接 https://$DOMAIN"
echo " 本机联调：注册表 HKCU\\Software\\2Box 下建 ServerUrl 字符串指向本地后端即可覆盖"
echo "============================================================"
