import { Router } from 'express';
import Joi from 'joi';
import { ok, fail, clientIp } from '../../middleware/helpers';
import { getConfigAll, getConfigValue, setConfigValue } from '../../services/ConfigService';
import { writeOperationLog, cleanExpiredLogs } from '../../services/LogService';

const router = Router();

const schema = Joi.object({
  heartbeat_interval_hours: Joi.number().integer().min(1).max(24).optional(),
  offline_grace_days: Joi.number().integer().min(1).max(30).optional(),
  force_online_activate: Joi.string().valid('0', '1').optional(),
  notice: Joi.string().allow('').max(500).optional(),
  online_threshold_minutes: Joi.number().integer().min(1).max(1440).optional(),
  // 操作日志自动清理：保留天数 + 每日清理时间（HH:mm）
  log_retention_days: Joi.number().integer().min(1).max(365).optional(),
  log_clean_time: Joi.string().pattern(/^([01]\d|2[0-3]):[0-5]\d$/).optional(),
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
  await writeOperationLog(req.auth!.userId, '修改系统设置', '系统设置', JSON.stringify(value), clientIp(req));
  ok(res, await getConfigAll());
});

// 立即清理过期操作日志（按配置的保留天数）
router.post('/clean-logs', async (req, res) => {
  const days = Math.max(1, Number(await getConfigValue('log_retention_days')) || 1);
  const n = await cleanExpiredLogs(days);
  await writeOperationLog(
    req.auth!.userId,
    '清理过期日志',
    '操作日志',
    `删除 ${n} 条超过 ${days} 天的操作日志`,
    clientIp(req)
  );
  ok(res, { deleted: n });
});

export default router;
