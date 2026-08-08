import { Router } from 'express';
import Joi from 'joi';
import { ok, fail, clientIp } from '../middleware/helpers';
import { ApiError } from '../middleware/errorHandler';
import { clientActivate, clientHeartbeat, clientUnbind, findByCode, keyWithDetail } from '../services/KeyService';
import { decodeAndVerify } from '../crypto/licenseCodec';
import { getConfigValue } from '../services/ConfigService';
import { AppDataSource } from '../config/database';
import { Device } from '../entities/Device';

const router = Router();

// 防重放：code+nonce 缓存 10 分钟（进程内 Map，足够单实例场景）
const nonceCache = new Map<string, number>();
setInterval(() => {
  const now = Date.now();
  for (const [k, t] of nonceCache) {
    if (now - t > 10 * 60 * 1000) nonceCache.delete(k);
  }
}, 5 * 60 * 1000).unref?.();

function checkNonce(code: string, nonce: string, clientTime: number): boolean {
  const now = Date.now();
  // 客户端时间偏差 > 5 分钟视为异常
  if (Math.abs(now / 1000 - clientTime) > 300) return false;
  const key = `${code}|${nonce}`;
  if (nonceCache.has(key)) return false;
  nonceCache.set(key, now);
  return true;
}

const bodySchema = Joi.object({
  code: Joi.string().required().max(200),
  machineFp: Joi.string().required().length(16).pattern(/^[0-9a-f]{16}$/),
  appVersion: Joi.string().allow('').max(32).default(''),
  os: Joi.string().allow('').max(128).default(''),
  timestamp: Joi.number().integer().required(),
  nonce: Joi.string().required().max(64),
});

// POST /api/v1/activate
router.post('/activate', async (req, res) => {
  try {
    const { error, value } = bodySchema.validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);
    const forceOnline = (await getConfigValue('force_online_activate')) === '1';
    if (!checkNonce(value.code, value.nonce, value.timestamp)) {
      if (forceOnline) return fail(res, '请求异常或已过期', 401);
    }
    const result = await clientActivate(value.code, value.machineFp, {
      ip: clientIp(req),
      os: value.os,
      appVersion: value.appVersion,
      clientTime: value.timestamp,
    });
    ok(res, result);
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// POST /api/v1/heartbeat
router.post('/heartbeat', async (req, res) => {
  try {
    const { error, value } = bodySchema.validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);
    if (!checkNonce(value.code, value.nonce, value.timestamp)) return fail(res, '请求异常或已过期', 401);
    const result = await clientHeartbeat(value.code, value.machineFp, {
      ip: clientIp(req),
      appVersion: value.appVersion,
      clientTime: value.timestamp,
    });
    ok(res, result);
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// POST /api/v1/unbind
router.post('/unbind', async (req, res) => {
  try {
    const { error, value } = bodySchema.validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);
    if (!checkNonce(value.code, value.nonce, value.timestamp)) return fail(res, '请求异常或已过期', 401);
    const result = await clientUnbind(value.code, value.machineFp, {
      ip: clientIp(req),
      appVersion: value.appVersion,
      clientTime: value.timestamp,
    });
    if (result.exceed) return fail(res, '本月解绑次数已达上限', 1006);
    ok(res, result);
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// GET /api/v1/status?code=&machineFp=
router.get('/status', async (req, res) => {
  try {
    const code = String(req.query.code || '');
    const fp = String(req.query.machineFp || '');
    const parsed = decodeAndVerify(code);
    if (!parsed) return fail(res, '激活码无效', 1001);
    const key = await findByCode(code);
    const serverTime = Math.floor(Date.now() / 1000);
    if (!key) {
      return ok(res, { online: false, registered: false, serverTime });
    }
    const detail = await keyWithDetail(key.id);
    const device = await AppDataSource.getRepository(Device).findOneBy({ keyId: key.id, machineFp: fp });
    return ok(res, {
      online: true,
      registered: true,
      status: key.status,
      type: key.type,
      durationSec: Number(key.durationSec || 0),
      expireAt: key.expireAt ? Number(key.expireAt) : null,
      bound: key.bound === 1,
      unbindMax: key.unbindMax,
      usedAt: key.usedAt,
      deviceId: device?.id || null,
      serverTime,
      detail,
    });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

export default router;
