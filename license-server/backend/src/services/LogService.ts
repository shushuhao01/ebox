import { AppDataSource } from '../config/database';
import { OperationLog } from '../entities/OperationLog';
import { Heartbeat } from '../entities/Heartbeat';

export async function writeOperationLog(
  userId: string,
  action: string,
  target: string | null,
  detail: string | null,
  ip: string
): Promise<void> {
  const row = AppDataSource.getRepository(OperationLog).create({
    userId,
    action,
    target,
    detail,
    ip,
  });
  await AppDataSource.getRepository(OperationLog).save(row);
}

/** 计算保留截止时间：当前时间 - 保留天数 */
function retentionCutoff(retentionDays?: number): Date {
  const days = Math.max(1, retentionDays || 1);
  return new Date(Date.now() - days * 24 * 3600 * 1000);
}

/**
 * 清理超过保留天数的操作日志，返回删除条数。
 * @param retentionDays 保留天数（<=0 时使用默认 1 天）
 */
export async function cleanExpiredLogs(retentionDays?: number): Promise<number> {
  const res = await AppDataSource.getRepository(OperationLog)
    .createQueryBuilder()
    .delete()
    .where('created_at < :cutoff', { cutoff: retentionCutoff(retentionDays) })
    .execute();
  return res.affected || 0;
}

/**
 * 清理超过保留天数的心跳日志（设备每 6 小时上报一条，量大需纳入自动清理避免爆盘）。
 * @param retentionDays 保留天数（<=0 时使用默认 1 天）
 */
export async function cleanExpiredHeartbeats(retentionDays?: number): Promise<number> {
  const res = await AppDataSource.getRepository(Heartbeat)
    .createQueryBuilder()
    .delete()
    .where('created_at < :cutoff', { cutoff: retentionCutoff(retentionDays) })
    .execute();
  return res.affected || 0;
}

/** 汇总清理结果：操作日志 + 心跳日志 */
export interface LogCleanResult {
  operationLogs: number;
  heartbeats: number;
}

/**
 * 按保留天数清理全部日志（操作日志 + 心跳日志）。
 * @param retentionDays 保留天数（<=0 时使用默认 1 天）
 */
export async function cleanExpiredData(retentionDays?: number): Promise<LogCleanResult> {
  const [operationLogs, heartbeats] = await Promise.all([
    cleanExpiredLogs(retentionDays),
    cleanExpiredHeartbeats(retentionDays),
  ]);
  return { operationLogs, heartbeats };
}
