import { AppDataSource } from '../config/database';
import { LicenseKey } from '../entities/LicenseKey';
import { Device } from '../entities/Device';
import { Heartbeat } from '../entities/Heartbeat';
import { UnbindLog } from '../entities/UnbindLog';
import { getConfigValue } from './ConfigService';

const keyRepo = () => AppDataSource.getRepository(LicenseKey);
const deviceRepo = () => AppDataSource.getRepository(Device);
const hbRepo = () => AppDataSource.getRepository(Heartbeat);

export async function overview() {
  const total = await keyRepo().createQueryBuilder('k').where('k.status != 5').getCount(); // 不含已删除
  const used = await keyRepo().countBy({ status: 1 });
  const revoked = await keyRepo().countBy({ status: 2 });
  const switched = await keyRepo().countBy({ status: 4 });
  const onlineThresholdMin = parseInt(await getConfigValue('online_threshold_minutes'), 10) || 30;
  const threshold = new Date(Date.now() - onlineThresholdMin * 60 * 1000);
  const onlineDevices = await deviceRepo()
    .createQueryBuilder('d')
    .where('d.status = 1 AND d.last_online_at >= :t', { t: threshold })
    .getCount();
  const startOfDay = new Date();
  startOfDay.setHours(0, 0, 0, 0);
  const todayActivate = await hbRepo().countBy({ action: 1, createdAt: startOfDay });
  return { total, used, revoked, switched, onlineDevices, todayActivate };
}

export async function trend(days: number): Promise<{ dates: string[]; activates: number[]; heartbeats: number[] }> {
  const start = new Date();
  start.setHours(0, 0, 0, 0);
  start.setDate(start.getDate() - (days - 1));

  const activates: Record<string, number> = {};
  const heartbeats: Record<string, number> = {};
  const dates: string[] = [];
  for (let i = 0; i < days; i++) {
    const d = new Date(start);
    d.setDate(start.getDate() + i);
    const k = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
    dates.push(k);
    activates[k] = 0;
    heartbeats[k] = 0;
  }

  const actRows = await hbRepo()
    .createQueryBuilder('h')
    .select("DATE_FORMAT(h.created_at, '%Y-%m-%d')", 'd')
    .addSelect('COUNT(*)', 'c')
    .where('h.action = 1 AND h.created_at >= :s', { s: start })
    .groupBy('d')
    .getRawMany<{ d: string; c: string }>();
  for (const r of actRows) activates[r.d] = Number(r.c);

  const hbRows = await hbRepo()
    .createQueryBuilder('h')
    .select("DATE_FORMAT(h.created_at, '%Y-%m-%d')", 'd')
    .addSelect('COUNT(*)', 'c')
    .where('h.action = 2 AND h.created_at >= :s', { s: start })
    .groupBy('d')
    .getRawMany<{ d: string; c: string }>();
  for (const r of hbRows) heartbeats[r.d] = Number(r.c);

  return {
    dates,
    activates: dates.map((d) => activates[d] ?? 0),
    heartbeats: dates.map((d) => heartbeats[d] ?? 0),
  };
}

export async function distribution() {
  // 时长分布（库存码）
  const durationRows = await keyRepo()
    .createQueryBuilder('k')
    .select('k.duration_sec', 'sec')
    .addSelect('COUNT(*)', 'c')
    .where('k.type = 1')
    .andWhere('k.status != 5') // 不含已删除
    .groupBy('k.duration_sec')
    .getRawMany<{ sec: string; c: string }>();
  const durationMap: Record<string, number> = {};
  for (const r of durationRows) {
    const sec = Number(r.sec);
    const days = Math.floor(sec / 86400);
    const label =
      days === 0 ? '永久' : days % 365 === 0 ? `${days / 365}年` : `${days}天`;
    durationMap[label] = (durationMap[label] || 0) + Number(r.c);
  }

  // 状态占比
  const statusRows = await keyRepo()
    .createQueryBuilder('k')
    .select('k.status', 's')
    .addSelect('COUNT(*)', 'c')
    .groupBy('k.status')
    .getRawMany<{ s: string; c: string }>();
  const statusNames: Record<string, string> = { 0: '未用', 1: '已用', 2: '作废', 3: '过期', 4: '已换机', 5: '已删除' };
  const statusMap: Record<string, number> = {};
  for (const r of statusRows) {
    statusMap[statusNames[r.s] || r.s] = Number(r.c);
  }

  // 最近 30 天激活来源分布（IP 统计不可靠，用码类型近似）
  const unbindCount = await AppDataSource.getRepository(UnbindLog).count();
  return { durationMap, statusMap, unbindCount };
}

export async function daily(limit = 10) {
  return hbRepo().find({
    order: { createdAt: 'DESC' },
    take: limit,
    relations: ['keyId'],
  });
}
