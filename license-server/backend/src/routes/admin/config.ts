import { Router } from 'express';
import Joi from 'joi';
import { ok, fail, clientIp } from '../../middleware/helpers';
import { getConfigAll, setConfigValue } from '../../services/ConfigService';
import { writeOperationLog } from '../../services/LogService';

const router = Router();

const schema = Joi.object({
  heartbeat_interval_hours: Joi.number().integer().min(1).max(24).optional(),
  offline_grace_days: Joi.number().integer().min(1).max(30).optional(),
  force_online_activate: Joi.string().valid('0', '1').optional(),
  notice: Joi.string().allow('').max(500).optional(),
  online_threshold_minutes: Joi.number().integer().min(1).max(1440).optional(),
});

router.get('/', async (_req, res) => {
  ok(res, await getConfigAll());
});

router.put('/', async (req, res) => {
  const { error, value } = schema.validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  for (const [k, v] of Object.entries(value)) {
    await setConfigValue(k, String(v));
  }
  await writeOperationLog(req.auth!.userId, '修改系统设置', null, JSON.stringify(value), clientIp(req));
  ok(res, await getConfigAll());
});

export default router;
