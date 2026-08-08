import { Router } from 'express';
import { AppDataSource } from '../../config/database';
import { Heartbeat } from '../../entities/Heartbeat';
import { UnbindLog } from '../../entities/UnbindLog';
import { RevokeLog } from '../../entities/RevokeLog';
import { OperationLog } from '../../entities/OperationLog';
import { LicenseKey } from '../../entities/LicenseKey';
import { ok } from '../../middleware/helpers';

const router = Router();

async function paged(qb: any, req: any) {
  const page = Math.max(1, parseInt(String(req.query.page || '1'), 10) || 1);
  const pageSize = Math.min(200, Math.max(1, parseInt(String(req.query.pageSize || '20'), 10) || 20));
  const total = await qb.getCount();
  const list = await qb.orderBy('t.createdAt', 'DESC').skip((page - 1) * pageSize).take(pageSize).getMany();
  return { total, page, pageSize, list };
}

// GET /api/admin/heartbeats
router.get('/heartbeats', async (req, res) => {
  const qb = AppDataSource.getRepository(Heartbeat)
    .createQueryBuilder('t')
    .leftJoin(LicenseKey, 'k', 'k.id = t.key_id');
  const action = String(req.query.action || '');
  if (action && action !== 'all') qb.andWhere('t.action = :a', { a: Number(action) });
  const code = String(req.query.code || '').trim();
  if (code) qb.andWhere('k.code LIKE :kw', { kw: `%${code}%` });
  ok(res, await paged(qb, req));
});

// GET /api/admin/unbind-logs
router.get('/unbind-logs', async (req, res) => {
  const qb = AppDataSource.getRepository(UnbindLog)
    .createQueryBuilder('t')
    .leftJoin(LicenseKey, 'k', 'k.id = t.key_id')
    .leftJoin(LicenseKey, 'nk', 'nk.id = t.new_key_id');
  const month = String(req.query.month || '');
  if (month) qb.andWhere('t.month = :m', { m: month });
  ok(res, await paged(qb, req));
});

// GET /api/admin/revoke-logs
router.get('/revoke-logs', async (req, res) => {
  const qb = AppDataSource.getRepository(RevokeLog)
    .createQueryBuilder('t')
    .leftJoin(LicenseKey, 'k', 'k.id = t.key_id');
  ok(res, await paged(qb, req));
});

// GET /api/admin/operation-logs
router.get('/operation-logs', async (req, res) => {
  const qb = AppDataSource.getRepository(OperationLog).createQueryBuilder('t');
  const action = String(req.query.action || '');
  if (action && action !== 'all') qb.andWhere('t.action = :a', { a: action });
  const userId = String(req.query.userId || '');
  if (userId) qb.andWhere('t.user_id = :u', { u: userId });
  ok(res, await paged(qb, req));
});

export default router;
