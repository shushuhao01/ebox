import { Router } from 'express';
import { AppDataSource } from '../../config/database';
import { Device } from '../../entities/Device';
import { Heartbeat } from '../../entities/Heartbeat';
import { ok } from '../../middleware/helpers';
import { overview, trend, distribution } from '../../services/StatsService';

const router = Router();

router.get('/overview', async (_req, res) => {
  ok(res, await overview());
});

router.get('/trend', async (req, res) => {
  const days = Math.min(90, Math.max(1, parseInt(String(req.query.days || '30'), 10) || 30));
  ok(res, await trend(days));
});

router.get('/distribution', async (_req, res) => {
  ok(res, await distribution());
});

// 最近心跳流水（总览用）
router.get('/recent-heartbeats', async (req, res) => {
  const limit = Math.min(50, parseInt(String(req.query.limit || '10'), 10) || 10);
  const rows = await AppDataSource.getRepository(Heartbeat).find({
    order: { createdAt: 'DESC' },
    take: limit,
  });
  ok(res, rows);
});

export default router;
