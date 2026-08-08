import 'reflect-metadata';
import express from 'express';
import cors from 'cors';
import helmet from 'helmet';
import morgan from 'morgan';
import compression from 'compression';
import rateLimit from 'express-rate-limit';
import path from 'path';
import cron from 'node-cron';

import { env } from './config/env';
import { logger, log } from './config/logger';
import { initializeDatabase } from './config/database';
import { errorHandler, notFoundHandler } from './middleware/errorHandler';
import { authRequired } from './middleware/helpers';

import clientRoutes from './routes/client';
import adminAuthRoutes from './routes/admin/auth';
import adminKeyRoutes from './routes/admin/keys';
import adminCustomerRoutes from './routes/admin/customers';
import adminDeviceRoutes from './routes/admin/devices';
import adminStatsRoutes from './routes/admin/stats';
import adminConfigRoutes from './routes/admin/config';
import adminLogRoutes from './routes/admin/logs';
import adminBatchRoutes from './routes/admin/batches';
import adminUserRoutes from './routes/admin/users';
import { markExpiredKeys } from './services/KeyService';

const app = express();
const PORT = env.port;
const API_PREFIX = '/api/v1';
const ADMIN_PREFIX = '/api/admin';

// 信任代理（Nginx 反代后取真实 IP）
app.set('trust proxy', 1);

app.use(
  helmet({
    crossOriginEmbedderPolicy: false,
    contentSecurityPolicy: false,
  })
);
app.use(
  cors({
    origin: (origin, cb) => {
      if (!origin || env.corsOrigin.includes(origin)) return cb(null, true);
      return cb(null, true); // 客户端为桌面程序（无 Origin），管理面板走同源；宽松处理
    },
    credentials: false,
  })
);
app.use(compression());
app.use(express.json({ limit: '1mb' }));
app.use(express.urlencoded({ extended: true, limit: '1mb' }));

if (env.isProduction) {
  app.use(
    morgan('combined', {
      stream: { write: (m: string) => logger.http(m.trim()) },
    })
  );
}

// 通用限流
const generalLimiter = rateLimit({
  windowMs: 60 * 1000,
  max: 600,
  message: { code: 429, msg: '请求过于频繁，请稍后再试', data: null },
  standardHeaders: true,
  legacyHeaders: false,
  skip: (req) => req.path === '/health',
});
app.use(generalLimiter);

// 管理接口限流（更严格）
const adminLimiter = rateLimit({
  windowMs: 60 * 1000,
  max: 120,
  message: { code: 429, msg: '请求过于频繁，请稍后再试', data: null },
  standardHeaders: true,
  legacyHeaders: false,
});

// 健康检查
app.get('/health', (_req, res) => {
  res.json({ success: true, message: 'eBox License Server OK', timestamp: new Date().toISOString() });
});
app.get('/api/health', (_req, res) => {
  res.json({ success: true, message: 'eBox License Server OK', timestamp: new Date().toISOString() });
});

// ==================== 客户端接口（无需登录） ====================
app.use(API_PREFIX, clientRoutes);

// ==================== 管理面板接口（JWT） ====================
app.use(`${ADMIN_PREFIX}/auth`, adminLimiter, adminAuthRoutes);
app.use(`${ADMIN_PREFIX}`, authRequired, adminKeyRoutes);
app.use(`${ADMIN_PREFIX}`, authRequired, adminCustomerRoutes);
app.use(`${ADMIN_PREFIX}`, authRequired, adminDeviceRoutes);
app.use(`${ADMIN_PREFIX}/stats`, authRequired, adminStatsRoutes);
app.use(`${ADMIN_PREFIX}/config`, authRequired, adminConfigRoutes);
app.use(`${ADMIN_PREFIX}`, authRequired, adminLogRoutes);
app.use(`${ADMIN_PREFIX}`, authRequired, adminBatchRoutes);
app.use(`${ADMIN_PREFIX}/users`, authRequired, adminUserRoutes);

// 404 与错误处理
app.use(notFoundHandler);
app.use(errorHandler);

// ==================== 启动 ====================
async function main() {
  try {
    await initializeDatabase();
    log.info(`✅ 数据库连接成功：${env.db.database} @ ${env.db.host}:${env.db.port}`);

    // 定时任务：每小时清理过期换机码状态
    cron.schedule('0 * * * *', async () => {
      try {
        const n = await markExpiredKeys();
        if (n > 0) log.info(`定时清理：${n} 个换机码已过期`);
      } catch (e) {
        log.error('定时清理失败', e);
      }
    });

    app.listen(PORT, () => {
      log.info(`🚀 eBox 授权服务平台后端已启动：http://0.0.0.0:${PORT}`);
      log.info(`   客户端接口：${API_PREFIX}   管理接口：${ADMIN_PREFIX}`);
    });
  } catch (e) {
    log.error('启动失败', e);
    process.exit(1);
  }
}

// 优雅退出
process.on('SIGTERM', () => process.exit(0));
process.on('SIGINT', () => process.exit(0));

main();
