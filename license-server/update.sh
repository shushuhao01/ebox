#!/usr/bin/env bash
# ============================================================
# eBox 授权服务平台 - 更新部署脚本（宝塔/Linux，在服务器上执行）
# ------------------------------------------------------------
# 用法：
#   bash update.sh        （脚本已置于仓库根目录）
#   可选环境变量：APP_DIR / DOMAIN / NPM_REGISTRY
#
# 流程：环境检查 → 配置检查 → 域名/证书/Nginx 检查 → 备份配置
#      → git 拉取 → 数据库表检查/自动初始化 → 前后端构建
#      → PM2 启停 → 健康验证
#
# PM2 逻辑：
#   - 进程 abc222.cn-backend 已存在  → pm2 restart abc222.cn-backend
#   - 进程不存在（如服务器重启后）     → pm2 start ecosystem.config.js
#
# 出错处理：任意关键步骤失败都会输出错误定位 + 相关日志尾部详情，并退出
# ============================================================
set -u

# ---------- 可配置项 ----------
APP_DIR="${APP_DIR:-/www/wwwroot/license-server}"
DOMAIN="${DOMAIN:-abc222.cn}"
PM2_NAME="abc222.cn-backend"
BACKEND_PORT=3008
NPM_REGISTRY="${NPM_REGISTRY:-https://registry.npmmirror.com}"

# ---------- 颜色 ----------
GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; BLUE='\033[0;34m'; CYAN='\033[0;36m'; NC='\033[0m'
ok()   { echo -e "${GREEN}✅ $1${NC}"; }
info() { echo -e "${BLUE}💡 $1${NC}"; }
warn() { echo -e "${YELLOW}⚠️  $1${NC}"; }
step() { echo ""; echo -e "${CYAN}━━━ $1 ━━━${NC}"; }

# 失败退出：输出错误定位 + 日志尾部详情
fail() {
    echo -e "${RED}❌ $1${NC}"
    if [ -n "${2:-}" ] && [ -f "$2" ]; then
        echo -e "${YELLOW}--- 相关日志尾部 ($2) ---${NC}"
        tail -n 30 "$2"
        echo -e "${YELLOW}--- 日志结束 ---${NC}"
    fi
    exit 1
}

# 关键步骤包装：失败时回显日志
run() {
    local desc="$1"; shift
    echo -e "${CYAN}⏳ $desc ...${NC}"
    if ! "$@" > /tmp/ebox-step.log 2>&1; then
        echo -e "${YELLOW}--- 命令输出尾部 (/tmp/ebox-step.log) ---${NC}"
        tail -n 40 /tmp/ebox-step.log
        echo -e "${YELLOW}--- 输出结束 ---${NC}"
        return 1
    fi
    return 0
}

# 展示两次提交间的文件变更：A=新增(绿) D=删除(红) M/R/C=修改(黄)
show_git_changes() {
    local old_head="$1"
    echo -e "${CYAN}--- 本次文件变更（A=新增  D=删除  M=修改）---${NC}"
    local stats
    stats=$(git diff --shortstat "$old_head" HEAD 2>/dev/null)
    if [ -z "$stats" ]; then
        echo "  （无文件变更，已是最新）"
        return
    fi
    echo "  $stats"
    git diff --name-status "$old_head" HEAD 2>/dev/null | while IFS=$'\t' read -r status path rest; do
        case "$status" in
            A)
                echo -e "  ${GREEN}A${NC}  ${path}"
                ;;
            D)
                echo -e "  ${RED}D${NC}  ${path}"
                ;;
            R*|C*)
                echo -e "  ${YELLOW}${status}${NC}  ${path} -> ${rest}"
                ;;
            *)
                echo -e "  ${YELLOW}${status}${NC}  ${path}"
                ;;
        esac
    done
}

echo "============================================================"
echo "🚀 eBox 授权服务平台 更新开始"
echo "    应用目录: $APP_DIR"
echo "    域名:     $DOMAIN"
echo "    PM2 进程: $PM2_NAME"
echo "============================================================"

# ============================================================
# 步骤 0：环境检查
# ============================================================
step "0/10 环境检查"
for cmd in node npm git pm2 curl openssl; do
    command -v "$cmd" >/dev/null 2>&1 || fail "缺少命令: $cmd，请先安装（宝塔软件商店 -> Node.js / pm2 / git）"
done
NODE_MAJOR="$(node -v | sed 's/v\([0-9]*\).*/\1/')"
[ "$NODE_MAJOR" -ge 22 ] || fail "需要 Node >= 22（当前 $(node -v)），请用宝塔 Node 版本管理器切换到 22.x"
ok "Node $(node -v) / npm $(npm -v)"

# 构建内存限制：按物理内存的 60% 分配给 Node 堆（v8 --max-old-space-size）
# 保底 1024MB、上限 4096MB：太小容易 OOM 构建失败，太大挤占系统内存导致卡死
TOTAL_MEM=$(free -m 2>/dev/null | awk '/^Mem:/{print $2}' || echo 4096)
HEAP_MB=$(( TOTAL_MEM * 60 / 100 ))
if [ "$HEAP_MB" -lt 1024 ]; then HEAP_MB=1024; fi
if [ "$HEAP_MB" -gt 4096 ]; then HEAP_MB=4096; fi
export NODE_OPTIONS="--max-old-space-size=$HEAP_MB"
ok "构建内存: 物理内存 ${TOTAL_MEM}MB 的 60% -> Node 堆上限 ${HEAP_MB}MB"

# ============================================================
# 步骤 1：代码目录检查
# ============================================================
step "1/10 代码目录检查"
[ -d "$APP_DIR" ] || fail "应用目录不存在: $APP_DIR（请先在宝塔建站并 clone 代码）"
[ -d "$APP_DIR/backend" ] || fail "未找到 $APP_DIR/backend"
[ -d "$APP_DIR/admin" ]   || fail "未找到 $APP_DIR/admin"
[ -f "$APP_DIR/backend/ecosystem.config.js" ] || fail "未找到 backend/ecosystem.config.js"
cd "$APP_DIR" || fail "无法进入 $APP_DIR"
ok "目录结构正常"

# ============================================================
# 步骤 2：配置文件检查（.env）
# ============================================================
step "2/10 配置文件检查 (.env)"
ENV_FILE="$APP_DIR/backend/.env"
if [ ! -f "$ENV_FILE" ]; then
    if [ -f "$APP_DIR/backend/.env.example" ]; then
        cp "$APP_DIR/backend/.env.example" "$ENV_FILE"
        warn "未找到 .env，已从 .env.example 生成"
        warn "【必须】编辑 $ENV_FILE：DB_PASSWORD / JWT_SECRET（LICENSE_PRIVATE_KEY_D 保留默认出厂密钥）"
        fail "请先编辑 .env 后再执行本脚本"
    else
        fail "未找到 backend/.env 和 .env.example"
    fi
fi
grep -q '^DB_PASSWORD=请修改为强密码' "$ENV_FILE" && fail "【配置检查】.env 中 DB_PASSWORD 仍是默认占位值，请修改后再更新"
grep -q '^JWT_SECRET=请修改为随机长字符串' "$ENV_FILE" && fail "【配置检查】.env 中 JWT_SECRET 仍是默认占位值，请修改后再更新"
grep -q '^LICENSE_PRIVATE_KEY_D=.\+' "$ENV_FILE" || fail "【配置检查】.env 缺少 LICENSE_PRIVATE_KEY_D（需与 eBox 客户端公钥配对）"
ok ".env 关键配置已就绪"

# ============================================================
# 步骤 3：域名解析 / SSL 证书 / Nginx 检查
# ============================================================
step "3/10 域名 / 证书 / Nginx 检查"

# 3.1 域名解析
DOMAIN_IP=$(getent hosts "$DOMAIN" | awk '{print $1}' | head -n1)
if [ -z "$DOMAIN_IP" ]; then
    warn "域名 $DOMAIN 无法解析！请先在域名服务商配置 A 记录指向本服务器"
else
    LOCAL_IP=$(curl -s --max-time 5 https://api.ipify.org 2>/dev/null || curl -s --max-time 5 https://ifconfig.me 2>/dev/null || true)
    if [ -n "$LOCAL_IP" ] && [ "$LOCAL_IP" = "$DOMAIN_IP" ]; then
        ok "域名 $DOMAIN → $DOMAIN_IP（与本机公网 IP 一致）"
    elif [ -n "$LOCAL_IP" ]; then
        warn "域名 $DOMAIN → $DOMAIN_IP，本机公网 IP $LOCAL_IP（如配置了 CDN/多级反代可忽略）"
    else
        ok "域名 $DOMAIN 已解析到 $DOMAIN_IP"
    fi
fi

# 3.2 SSL 证书（宝塔 Let's Encrypt 默认路径）
CERT_FILE="/www/server/panel/vhost/cert/$DOMAIN/fullchain.pem"
if [ -f "$CERT_FILE" ]; then
    CERT_DATE=$(openssl x509 -enddate -noout -in "$CERT_FILE" 2>/dev/null | sed 's/notAfter=//')
    CERT_EPOCH=$(date -d "$CERT_DATE" +%s 2>/dev/null || echo 0)
    NOW_EPOCH=$(date +%s)
    if [ "$CERT_EPOCH" -gt 0 ]; then
        DAYS=$(( (CERT_EPOCH - NOW_EPOCH) / 86400 ))
        if [ "$DAYS" -lt 0 ]; then
            fail "【证书检查】$DOMAIN 证书已过期（$CERT_DATE），请到宝塔 SSL 申请新证书"
        elif [ "$DAYS" -lt 15 ]; then
            warn "【证书检查】$DOMAIN 证书 $DAYS 天后到期（$CERT_DATE），建议续期"
        else
            ok "SSL 证书有效，剩余 ${DAYS} 天（到期 $CERT_DATE）"
        fi
    else
        warn "证书文件存在但无法解析到期时间: $CERT_FILE"
    fi
else
    warn "未找到证书文件 $CERT_FILE（若用自定义证书路径，请自行核对 deploy/nginx/abc222.cn.conf）"
fi

# 3.3 Nginx 配置
NGINX_BIN=$(command -v nginx 2>/dev/null || echo /www/server/nginx/sbin/nginx)
if [ -x "$NGINX_BIN" ]; then
    if "$NGINX_BIN" -t > /tmp/ebox-nginx.log 2>&1; then
        ok "Nginx 配置语法正确"
    else
        tail -n 10 /tmp/ebox-nginx.log
        fail "【Nginx 检查】nginx -t 校验失败，请先修复 Nginx 配置"
    fi
    NGINX_CONF=$(find /www/server/panel/vhost/nginx -maxdepth 1 -name '*.conf' 2>/dev/null | xargs grep -l "$DOMAIN" 2>/dev/null | head -n1)
    if [ -n "$NGINX_CONF" ]; then
        grep -q 'location /api/' "$NGINX_CONF" 2>/dev/null || warn "【Nginx 检查】$NGINX_CONF 未包含 location /api/ 反代（客户端/面板接口会 404）"
        grep -q 'admin/dist' "$NGINX_CONF" 2>/dev/null || warn "【Nginx 检查】$NGINX_CONF 未包含 admin/dist 静态目录（面板会白屏）"
        grep -q 'proxy_pass' "$NGINX_CONF" 2>/dev/null || warn "【Nginx 检查】$NGINX_CONF 未包含 proxy_pass（后端接口不可达）"
    else
        warn "未找到 $DOMAIN 的宝塔 Nginx 站点配置，请确认已按 deploy/nginx/abc222.cn.conf 配置"
    fi
else
    warn "未找到 nginx 命令，跳过 Nginx 检查（若使用宝塔，路径通常在 /www/server/nginx/sbin/nginx）"
fi

# ============================================================
# 步骤 4：备份 .env（防止 pull 被 gitignore 规则影响/丢失）
# ============================================================
step "4/10 备份配置文件"
cp "$ENV_FILE" "$APP_DIR/backend/.env.update-bak" && ok "已备份 .env -> backend/.env.update-bak"

# ============================================================
# 步骤 5：git 拉取最新代码
# ============================================================
step "5/10 拉取最新代码"
if ! git diff --quiet; then
    warn "检测到本地未提交修改，先 stash 保存再拉取"
    git stash push -m "auto-stash before update $(date '+%F %T')" >/dev/null || fail "git stash 失败"
    STASHED=1
fi
OLD_HEAD=$(git rev-parse HEAD)
if ! git pull origin main > /tmp/ebox-pull.log 2>&1; then
    tail -n 20 /tmp/ebox-pull.log
    [ -n "${STASHED:-}" ] && git stash pop >/dev/null 2>&1 || true
    fail "git pull 失败（见上方输出）"
fi
ok "代码已更新到最新"
# 展示本次更新的文件变更（新增=绿 删除=红 修改=黄）
show_git_changes "$OLD_HEAD"
info "本次更新提交："
git log --oneline -3
[ -n "${STASHED:-}" ] && { git stash pop >/dev/null 2>&1 || warn "stash 恢复失败，改动在 git stash list 中"; }

# ============================================================
# 步骤 6：数据库表检查与自动初始化
# 防止建表缺失导致登录/激活报 "Table doesn't exist"（首次部署常见问题）
# ============================================================
step "6/10 数据库初始化检查"

DB_HOST=$(grep -E '^DB_HOST=' "$ENV_FILE" | head -n1 | sed 's/^DB_HOST=//' | tr -d '"' | tr -d "'")
DB_PORT=$(grep -E '^DB_PORT=' "$ENV_FILE" | head -n1 | sed 's/^DB_PORT=//' | tr -d '"' | tr -d "'")
DB_USER=$(grep -E '^DB_USER=' "$ENV_FILE" | head -n1 | sed 's/^DB_USER=//' | tr -d '"' | tr -d "'")
DB_PASSWORD=$(grep -E '^DB_PASSWORD=' "$ENV_FILE" | head -n1 | sed 's/^DB_PASSWORD=//' | tr -d '"' | tr -d "'")
DB_DATABASE=$(grep -E '^DB_DATABASE=' "$ENV_FILE" | head -n1 | sed 's/^DB_DATABASE=//' | tr -d '"' | tr -d "'")
DB_HOST="${DB_HOST:-127.0.0.1}"; DB_PORT="${DB_PORT:-3306}"
DB_USER="${DB_USER:-license}"; DB_DATABASE="${DB_DATABASE:-license_server}"

if ! command -v mysql >/dev/null 2>&1; then
    warn "未找到 mysql 命令，跳过数据库表检查（请确认宝塔 MySQL 已安装，并手动导入 database/schema.sql）"
else
    TABLE_COUNT=$(MYSQL_PWD="$DB_PASSWORD" mysql -h"$DB_HOST" -P"$DB_PORT" -u"$DB_USER" "$DB_DATABASE" \
        -N -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$DB_DATABASE';" 2>/tmp/ebox-db.log)
    # 核心表检查：license_keys / devices / heartbeats 缺一即视为结构不完整
    CORE_COUNT=$(MYSQL_PWD="$DB_PASSWORD" mysql -h"$DB_HOST" -P"$DB_PORT" -u"$DB_USER" "$DB_DATABASE" \
        -N -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$DB_DATABASE' AND table_name IN ('license_keys','devices','heartbeats');" 2>/dev/null)
    if [ -z "$TABLE_COUNT" ]; then
        warn "数据库 $DB_DATABASE 连接失败（@ $DB_HOST:$DB_PORT）："
        tail -n 5 /tmp/ebox-db.log
        warn "请确认：库已创建 / DB_PASSWORD 正确 / DB_USER 对该库有权限"
    elif [ "$TABLE_COUNT" -lt 8 ] || [ "${CORE_COUNT:-0}" -lt 3 ]; then
        warn "数据库 $DB_DATABASE 表结构不完整（表数 $TABLE_COUNT 张，核心表命中 ${CORE_COUNT:-0}/3），自动导入 database/schema.sql 补齐 ..."
        # 去掉 schema.sql 开头的 CREATE DATABASE / USE / 注释行，导入到 .env 指定的库。
        # schema.sql 全部为 CREATE TABLE IF NOT EXISTS，已存在的表自动跳过，不会影响已有数据
        sed -e '/^CREATE DATABASE /d' -e '/^USE `/d' -e '/^-- /d' "$APP_DIR/database/schema.sql" \
            | MYSQL_PWD="$DB_PASSWORD" mysql -h"$DB_HOST" -P"$DB_PORT" -u"$DB_USER" "$DB_DATABASE" \
            || fail "导入 database/schema.sql 失败（见上方 mysql 输出），可手动执行上述 sed 导入命令" /tmp/ebox-db.log
        TABLE_COUNT=$(MYSQL_PWD="$DB_PASSWORD" mysql -h"$DB_HOST" -P"$DB_PORT" -u"$DB_USER" "$DB_DATABASE" \
            -N -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$DB_DATABASE';")
        ok "schema.sql 导入完成，当前表数量：$TABLE_COUNT"
    else
        ok "数据库 $DB_DATABASE 表结构正常（$TABLE_COUNT 张表）"
    fi

    # 管理员确保存在（默认 admin/admin123，可用 .env 的 ADMIN_USERNAME / ADMIN_PASSWORD 覆盖）
    ADMIN_USER=$(grep -E '^ADMIN_USERNAME=' "$ENV_FILE" | head -n1 | sed 's/^ADMIN_USERNAME=//' | tr -d '"' | tr -d "'")
    ADMIN_USER="${ADMIN_USER:-admin}"
    ADMIN_PASS=$(grep -E '^ADMIN_PASSWORD=' "$ENV_FILE" | head -n1 | sed 's/^ADMIN_PASSWORD=//' | tr -d '"' | tr -d "'")
    ADMIN_PASS="${ADMIN_PASS:-admin123}"
    ADMIN_COUNT=$(MYSQL_PWD="$DB_PASSWORD" mysql -h"$DB_HOST" -P"$DB_PORT" -u"$DB_USER" "$DB_DATABASE" \
        -N -e "SELECT COUNT(*) FROM users WHERE username='$ADMIN_USER';" 2>/dev/null)
    if [ "$ADMIN_COUNT" = "0" ]; then
        info "数据库无管理员 $ADMIN_USER，自动创建（密码: $ADMIN_PASS，请尽快在面板修改）..."
        if (cd "$APP_DIR/backend" && npm run init:admin -- --username "$ADMIN_USER" --password "$ADMIN_PASS") > /tmp/ebox-admin.log 2>&1; then
            ok "管理员 $ADMIN_USER 创建成功"
        else
            tail -n 15 /tmp/ebox-admin.log
            warn "管理员创建失败，可手动执行：cd backend && npm run init:admin -- --username admin --password xxxx"
        fi
    else
        ok "管理员 $ADMIN_USER 已存在"
    fi
fi

# ============================================================
# 步骤 7：后端依赖 + 构建
# ============================================================
step "7/10 后端依赖与构建"
cd "$APP_DIR/backend" || fail "无法进入 backend"
run "安装后端依赖" npm install --registry="$NPM_REGISTRY" || fail "后端依赖安装失败" /tmp/ebox-step.log
run "构建后端" npm run build || fail "后端构建失败（tsc 编译报错，见上方输出）" /tmp/ebox-step.log
[ -f dist/app.js ] || fail "后端构建产物缺失: backend/dist/app.js"
ok "后端构建完成: dist/app.js"

# ============================================================
# 步骤 8：前端依赖 + 构建
# ============================================================
step "8/10 前端依赖与构建"
cd "$APP_DIR/admin" || fail "无法进入 admin"
run "安装前端依赖" npm install --registry="$NPM_REGISTRY" || fail "前端依赖安装失败" /tmp/ebox-step.log
run "构建前端" npm run build || fail "前端构建失败（vite 编译报错，见上方输出）" /tmp/ebox-step.log
[ -f dist/index.html ] || fail "前端构建产物缺失: admin/dist/index.html"
ok "前端构建完成: admin/dist"

# ============================================================
# 步骤 9：PM2 启停（有进程重启 / 无进程启动）
# ============================================================
step "9/10 PM2 服务启停"
cd "$APP_DIR/backend" || fail "无法进入 backend"

PM2_ID=$(pm2 id "$PM2_NAME" 2>/dev/null | tr -d '[] ')
if [ -n "$PM2_ID" ]; then
    info "检测到进程 $PM2_NAME (id=$PM2_ID)，执行重启"
    run "重启 PM2 进程" pm2 restart "$PM2_NAME" --update-env || fail "PM2 重启失败" /tmp/ebox-step.log
else
    info "PM2 无该进程（服务器重启/首次），执行启动 ecosystem.config.js"
    run "启动 PM2 进程" pm2 start ecosystem.config.js --update-env || fail "PM2 启动失败（请检查 backend/logs/pm2-error.log）" /tmp/ebox-step.log
fi
pm2 save >/dev/null 2>&1 || true
ok "PM2 进程已就绪"

# ------------------------------------------------------------
# 9.5 PM2 日志轮转：pm2 捕获的 stdout/stderr 日志（~/.pm2/logs）
#     会无限增长导致爆盘，必须按大小自动切割。配置为满 50M 切割、保留 5 份。
# ------------------------------------------------------------
step "9.5/10 PM2 日志轮转配置（满 50M 自动切割）"
if pm2 describe pm2-logrotate >/dev/null 2>&1; then
    info "pm2-logrotate 已安装，更新轮转参数..."
else
    run "安装 pm2-logrotate" pm2 install pm2-logrotate || warn "pm2-logrotate 安装失败，可手动执行：pm2 install pm2-logrotate"
fi
pm2 set pm2-logrotate:max_size 50M >/dev/null 2>&1 || true
pm2 set pm2-logrotate:retain 5 >/dev/null 2>&1 || true
pm2 set pm2-logrotate:compress true >/dev/null 2>&1 || true
pm2 set pm2-logrotate:rotateInterval '0 0 * * *' >/dev/null 2>&1 || true
ok "PM2 日志轮转已配置：单文件最大 50M、保留 5 份、每天 0 点检查切割"

# 等待端口就绪
for i in $(seq 1 20); do
    if curl -sf "http://127.0.0.1:$BACKEND_PORT/health" >/dev/null 2>&1; then
        READY=1; break
    fi
    sleep 1
done
if [ -z "${READY:-}" ]; then
    tail -n 30 "$APP_DIR/backend/logs/pm2-error.log" 2>/dev/null || true
    fail "后端 $BACKEND_PORT 端口 20 秒内未就绪，最近错误日志见上方"
fi
ok "后端端口 $BACKEND_PORT 已就绪"

# ============================================================
# 步骤 10：健康验证
# ============================================================
step "10/10 健康验证"
echo "--- PM2 状态 ---"
pm2 list | grep -E "name|$PM2_NAME" || pm2 list
echo ""
echo "--- 本地 /health ---"
curl -sf "http://127.0.0.1:$BACKEND_PORT/health" && echo "" || warn "本地 /health 未响应"
echo ""
echo "--- 全链路 https://$DOMAIN/health ---"
if curl -sk --max-time 10 "https://$DOMAIN/health" > /tmp/ebox-health.log 2>&1; then
    cat /tmp/ebox-health.log; echo ""
    ok "全链路健康检查通过"
else
    warn "https://$DOMAIN/health 不可达（可能是证书/DNS/Nginx 未就绪，可先忽略本地验证）"
fi

# 更新成功：自动清理旧配置备份（若中途失败 fail 退出则不会执行到此处，备份保留以便排查）
rm -f "$APP_DIR/backend/.env.update-bak"
ok "已自动清理配置备份 backend/.env.update-bak"

echo ""
echo "============================================================"
echo -e "${GREEN}🎉 更新完成！${NC}"
echo "============================================================"
echo " 管理面板: https://$DOMAIN"
echo " 客户端:   默认连接 https://$DOMAIN（在线托管码可正常激活）"
echo " 查看日志: pm2 logs $PM2_NAME"
echo " 重启服务: pm2 restart $PM2_NAME"
echo "============================================================"
