import { AppDataSource } from '../config/database';
import { OperationLog } from '../entities/OperationLog';

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

/**
 * 清理超过保留天数的操作日志，返回删除条数。
 * @param retentionDays 保留天数（<=0 时使用默认 1 天）
 */
export async function cleanExpiredLogs(retentionDays?: number): Promise<number> {
  const days = Math.max(1, retentionDays || 1);
  const cutoff = new Date(Date.now() - days * 24 * 3600 * 1000);
  const res = await AppDataSource.getRepository(OperationLog)
    .createQueryBuilder()
    .delete()
    .where('created_at < :cutoff', { cutoff })
    .execute();
  return res.affected || 0;
}
