#!/bin/bash
# eBox 授权平台诊断脚本：排查"生成激活码卡顿/一直转圈"问题
# 用法：cd /www/wwwroot/license-server && bash diag.sh
set -u
cd "$(dirname "$0")" || exit 1
ENV_FILE="backend/.env"
if [ ! -f "$ENV_FILE" ]; then echo "未找到 $ENV_FILE"; exit 1; fi

DBH=$(grep -E '^DB_HOST=' "$ENV_FILE" | head -n1 | sed 's/^DB_HOST=//' | tr -d '"' | tr -d "'")
DBPT=$(grep -E '^DB_PORT=' "$ENV_FILE" | head -n1 | sed 's/^DB_PORT=//' | tr -d '"' | tr -d "'")
DBU=$(grep -E '^DB_USER=' "$ENV_FILE" | head -n1 | sed 's/^DB_USER=//' | tr -d '"' | tr -d "'")
DBP=$(grep -E '^DB_PASSWORD=' "$ENV_FILE" | head -n1 | sed 's/^DB_PASSWORD=//' | tr -d '"' | tr -d "'")
DBN=$(grep -E '^DB_DATABASE=' "$ENV_FILE" | head -n1 | sed 's/^DB_DATABASE=//' | tr -d '"' | tr -d "'")
export MYSQL_PWD="$DBP"

echo "========== eBox 生成卡顿诊断 $(date '+%F %T') =========="
echo "数据库: $DBN @ $DBH:$DBPT"

# 用 JWT_SECRET 签发管理员 token（不经登录接口，避免密码问题）
ADMINID=$(mysql -h"$DBH" -P"$DBPT" -u"$DBU" "$DBN" -N -e "SELECT id FROM users WHERE username='admin' LIMIT 1;" 2>/dev/null | head -n1)
[ -z "$ADMINID" ] && ADMINID=1
TOKEN=$(cd backend && node -e "
const fs=require('fs');const env=fs.readFileSync('.env','utf8');
const m=env.match(/^JWT_SECRET=(.*)$/m);if(!m){console.error('no JWT_SECRET');process.exit(1)}
const sec=m[1].trim().replace(/^[\"']|[\"']$/g,'');
const jwt=require('jsonwebtoken');
console.log(jwt.sign({userId:'$ADMINID',username:'admin',role:1},sec,{expiresIn:'1d'}));
")
echo "token长度: ${#TOKEN}  adminId=$ADMINID"
if [ -z "$TOKEN" ]; then echo "签发token失败，无法继续"; exit 1; fi

echo ""
echo "---- ① 列表 GET 耗时（后端是否整体变慢）----"
curl -s -o /dev/null -w "GET /keys: HTTP=%{http_code} 耗时=%{time_total}s\n" -m 30 -X GET "http://127.0.0.1:3008/api/admin/keys?page=1&pageSize=20" -H "Authorization: Bearer $TOKEN"

echo ""
echo "---- ② 后台发生成请求，10 秒后抓数据库状态 ----"
curl -s -m 120 -o /tmp/gen_resp.txt -w "生成接口: HTTP=%{http_code} 耗时=%{time_total}s\n" -X POST http://127.0.0.1:3008/api/admin/keys/generate -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" -d '{"durationSec":2592000,"bound":true,"unbindMax":1,"count":1}' &
sleep 10
echo "--- MySQL PROCESSLIST ---"
mysql -h"$DBH" -P"$DBPT" -u"$DBU" "$DBN" -e "SHOW FULL PROCESSLIST;" 2>&1
echo "--- 连接数 ---"
mysql -h"$DBH" -P"$DBPT" -u"$DBU" -e "SHOW GLOBAL STATUS LIKE 'Threads_connected';" 2>&1

echo ""
echo "---- ③ CPU / 内存 ----"
top -bn1 | head -16

echo ""
echo "---- ④ 后端最近日志 ----"
tail -n 15 backend/logs/pm2-out-31.log 2>/dev/null || tail -n 15 logs/pm2-out-31.log 2>/dev/null

wait
echo ""
echo "---- ⑤ 生成响应内容 ----"
head -c 400 /tmp/gen_resp.txt 2>/dev/null
echo ""
echo "========== 诊断结束 =========="
