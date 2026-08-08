import dotenv from 'dotenv';
import path from 'path';
import fs from 'fs';

// 环境加载：生产加载 .env；开发优先 .env.local，缺失回退 .env（对齐 CRM 做法）
const isProduction = process.env.NODE_ENV === 'production';
let envFile = '.env';
if (!isProduction) {
  const localPath = path.join(__dirname, '../../.env.local');
  if (fs.existsSync(localPath)) envFile = '.env.local';
}
dotenv.config({ path: path.join(__dirname, '../../', envFile) });

export const env = {
  isProduction,
  nodeEnv: process.env.NODE_ENV || 'development',
  port: parseInt(process.env.PORT || '3008', 10),

  db: {
    host: process.env.DB_HOST || '127.0.0.1',
    port: parseInt(process.env.DB_PORT || '3306', 10),
    user: process.env.DB_USER || 'root',
    password: process.env.DB_PASSWORD || '',
    database: process.env.DB_DATABASE || 'license_server',
  },

  jwtSecret: process.env.JWT_SECRET || 'change-me-please',
  jwtExpires: process.env.JWT_EXPIRES || '7d',

  // ECDSA P-256 私钥标量 d（hex，64 字符）。必须与 eBox 客户端内置公钥配对！
  // 生成方式：npm run gen:key 查看/回填
  privateKeyD: process.env.LICENSE_PRIVATE_KEY_D || '',

  // 默认配置
  heartbeatIntervalHours: parseInt(process.env.HEARTBEAT_INTERVAL_HOURS || '6', 10),
  offlineGraceDays: parseInt(process.env.OFFLINE_GRACE_DAYS || '7', 10),

  // CORS 白名单（逗号分隔）
  corsOrigin: (process.env.CORS_ORIGIN || 'http://localhost:5173,http://127.0.0.1:5173')
    .split(',').map((s) => s.trim()).filter(Boolean),
};

if (!env.privateKeyD) {
  // 启动告警但不阻断（避免私钥未配置导致服务不可用；签名接口会返回错误）
  console.warn('[env] 未配置 LICENSE_PRIVATE_KEY_D，签名/换机码接口将不可用，请执行 npm run gen:key');
}
