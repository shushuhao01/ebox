import { Router } from 'express';
import { MoreThanOrEqual } from 'typeorm';
import { AppDataSource } from '../../config/database';
import { Device } from '../../entities/Device';
import { LicenseKey } from '../../entities/LicenseKey';
import { ok, fail, clientIp } from '../../middleware/helpers';
import { ApiError } from '../../middleware/errorHandler';
import { getConfigValue } from '../../services/ConfigService';
import { writeOperationLog } from '../../services/LogService';

const router = Router();
const repo = () => AppDataSource.getRepository(Device);

// GET /api/admin/devices  分页筛选（在线/离线/锁定）
router.get('/devices', async (req, res) => {
  const page = Math.max(1, parseInt(String(req.query.page || '1'), 10) || 1);
  const pageSize = Math.min(100, Math.max(1, parseInt(String(req.query.pageSize || '20'), 10) || 20));
  const qb = repo()
    .createQueryBuilder('d')
    .leftJoin(LicenseKey, 'k', 'k.id = d.key_id')
    .where('1=1');

  const mode = String(req.query.mode || 'all');
  const thresholdMin = parseInt(await getConfigValue('online_threshold_minutes'), 10) || 30;
  const threshold = new Date(Date.now() - thresholdMin * 60 * 1000);
  if (mode === 'online') qb.andWhere('d.status = 1 AND d.last_online_at >= :t', { t: threshold });
  else if (mode === 'offline') qb.andWhere('(d.status = 1 AND d.last_online_at < :t)', { t: threshold });
  else if (mode === 'kicked') qb.andWhere('d.status = 2');
  else if (mode === 'unbound') qb.andWhere('d.status = 0');

  const search = String(req.query.search || '').trim();
  if (search) qb.andWhere('(d.machine_fp LIKE :kw OR k.code LIKE :kw OR d.last_ip LIKE :kw)', { kw: `%${search}%` });

  const total = await qb.getCount();
  const rows = await qb.orderBy('d.lastOnlineAt', 'DESC').skip((page - 1) * pageSize).take(pageSize).getMany();
  ok(res, { total, page, pageSize, list: rows });
});

// GET /api/admin/devices/online  在线设备（供总览）
router.get('/devices/online', async (req, res) => {
  const thresholdMin = parseInt(await getConfigValue('online_threshold_minutes'), 10) || 30;
  const threshold = new Date(Date.now() - thresholdMin * 60 * 1000);
  const rows = await repo().find({ where: { status: 1, lastOnlineAt: MoreThanOrEqual(threshold) }, order: { lastOnlineAt: 'DESC' }, take: 200 });
  ok(res, rows);
});

// POST /api/admin/devices/:id/kick  踢下线
router.post('/devices/:id/kick', async (req, res) => {
  try {
    const device = await repo().findOneBy({ id: req.params.id });
    if (!device) return fail(res, '设备不存在', 1002);
    device.status = 2;
    await repo().save(device);
    const key = device.keyId ? await AppDataSource.getRepository(LicenseKey).findOneBy({ id: device.keyId }) : null;
    await writeOperationLog(
      req.auth!.userId,
      '踢下线',
      key?.code || `设备#${device.id}`,
      `设备ID=#${device.id} 指纹=${device.machineFp}${device.appVersion ? ` 版本=${device.appVersion}` : ''}${device.lastIp ? ` 最近IP=${device.lastIp}` : ''}`,
      clientIp(req)
    );
    ok(res, { id: device.id });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

export default router;
