import { Request, Response, NextFunction } from 'express';
import jwt from 'jsonwebtoken';
import { env } from '../config/env';

// 统一响应：{ code, msg, data }
export function ok(res: Response, data?: unknown, msg = 'ok') {
  res.json({ code: 0, msg, data: data ?? null });
}
export function fail(res: Response, msg: string, code = 1, status = 200) {
  res.status(status).json({ code, msg, data: null });
}

export interface AuthPayload {
  userId: string;
  username: string;
  role: number;
}

declare global {
  // eslint-disable-next-line @typescript-eslint/no-namespace
  namespace Express {
    interface Request {
      auth?: AuthPayload;
    }
  }
}

export function signToken(payload: AuthPayload): string {
  // expiresIn 容错：纯数字当秒数；数字+单位(ms/s/m/h/d/w/y)原样用；非法/空值回退默认 7 天
  // 防止 .env 中 JWT_EXPIRES 配置异常导致登录/续期接口 500
  const rawExpires = (env.jwtExpires || '7d').trim();
  let expiresIn: string | number = rawExpires;
  if (/^\d+$/.test(rawExpires)) {
    expiresIn = Number(rawExpires);
  } else if (!/^\d+(ms|s|m|h|d|w|y)$/.test(rawExpires)) {
    expiresIn = '7d';
  }
  return jwt.sign(payload, env.jwtSecret, { expiresIn } as jwt.SignOptions);
}

export function authRequired(req: Request, res: Response, next: NextFunction) {
  const header = req.headers.authorization || '';
  const token = header.startsWith('Bearer ') ? header.slice(7) : '';
  if (!token) {
    res.status(401).json({ code: 401, msg: '未登录或登录已过期', data: null });
    return;
  }
  try {
    const decoded = jwt.verify(token, env.jwtSecret) as AuthPayload;
    req.auth = decoded;
    next();
  } catch {
    res.status(401).json({ code: 401, msg: '未登录或登录已过期', data: null });
  }
}

// 仅超管
export function superAdminRequired(req: Request, res: Response, next: NextFunction) {
  if (!req.auth || req.auth.role !== 1) {
    res.status(403).json({ code: 403, msg: '无权限：仅超级管理员可操作', data: null });
    return;
  }
  next();
}

// 客户端 IP（trust proxy 后取真实 IP）
export function clientIp(req: Request): string {
  const xf = req.headers['x-forwarded-for'];
  const ip = typeof xf === 'string' && xf.length ? xf.split(',')[0].trim() : req.ip || req.socket.remoteAddress || '';
  return normalizeIp(ip);
}

// 规范化 IP：IPv4 映射 IPv6（::ffff:1.2.3.4 → 1.2.3.4）；IPv6 回环 ::1 → 127.0.0.1
export function normalizeIp(ip: string): string {
  if (!ip) return '';
  const mapped = ip.match(/^::ffff:(\d+\.\d+\.\d+\.\d+)$/i);
  if (mapped) return mapped[1];
  if (ip === '::1') return '127.0.0.1';
  return ip;
}
