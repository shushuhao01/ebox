import winston from 'winston';
import path from 'path';
import fs from 'fs';

const logDir = path.join(process.cwd(), 'logs');
if (!fs.existsSync(logDir)) fs.mkdirSync(logDir, { recursive: true });

export const logger = winston.createLogger({
  level: process.env.NODE_ENV === 'production' ? 'info' : 'debug',
  format: winston.format.combine(
    winston.format.timestamp({ format: 'YYYY-MM-DD HH:mm:ss' }),
    winston.format.errors({ stack: true }),
    winston.format.printf(({ timestamp, level, message, ...meta }) => {
      const extra = Object.keys(meta).length ? ` ${JSON.stringify(meta)}` : '';
      return `[${timestamp}] ${level.toUpperCase()} ${message}${extra}`;
    })
  ),
  transports: [
    new winston.transports.Console(),
    new winston.transports.File({ filename: path.join(logDir, 'app.log'), maxsize: 10 * 1024 * 1024, maxFiles: 7 }),
    new winston.transports.File({ filename: path.join(logDir, 'error.log'), level: 'error', maxsize: 10 * 1024 * 1024, maxFiles: 7 }),
  ],
});

export const log = {
  info: (msg: string, meta?: unknown) => logger.info(msg, meta ?? {}),
  warn: (msg: string, meta?: unknown) => logger.warn(msg, meta ?? {}),
  error: (msg: string, meta?: unknown) => logger.error(msg, meta ?? {}),
  debug: (msg: string, meta?: unknown) => logger.debug(msg, meta ?? {}),
  http: (msg: string) => logger.http(msg),
};
