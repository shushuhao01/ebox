import { AppDataSource } from '../config/database';
import { OperationLog } from '../entities/OperationLog';
import { Heartbeat } from '../entities/Heartbeat';
import { logger } from '../config/logger';
import type { ObjectLiteral, Repository } from 'typeorm';

/**
 * 写入操作日志。
 * 带 8 秒硬超时 + 内部吞错：操作日志属于审计辅助，绝不能因为数据库连接/锁异常
 * 挂起整个业务请求（曾导致生成激活码后前端一直转圈、nginx 60s 超时）。
 * 失败时仅记录 error 日志，不影响主流程。
 */
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
  const TIMEOUT_MS = 8000;
  try {
    await Promise.race([
      AppDataSource.getRepository(OperationLog).save(row),
      new Promise<never>((_, reject) =>
        setTimeout(() => reject(new Error(`operation_logs 写入超时（${TIMEOUT_MS}ms）`)), TIMEOUT_MS)
      ),
    ]);
  } catch (e) {
    logger.error('操作日志写入失败（不影响主流程）', { err: (e as Error).message, action, target });
  }
}

/** 计算保留截止时间：当前时间 - 保留天数 */
function retentionCutoff(retentionDays?: number): Date {
  const days = Math.max(1, retentionDays || 1);
  return new Date(Date.now() - days * 24 * 3600 * 1000);
}

/**
 * 分批删除过期数据。
 * 一次性 DELETE 几十万行会长时间占用数据库连接并锁住整张表，
 * 阻塞业务写入（如生成激活码时的操作日志 INSERT），导致前端一直转圈。
 * 改为每批 500 行 + 批次间隔让出连接，避免长时间持锁。
 */
async function deleteExpiredInBatches<T extends ObjectLiteral>(
  repo: Repository<T>,
  cutoff: Date,
  batchSize = 500,
  maxBatches = 2000
): Promise<number> {
  const table = repo.metadata.tableName;
  let total = 0;
  for (let i = 0; i < maxBatches; i++) {
    // 原生 SQL：DELETE ... LIMIT 仅 MySQL 支持，分批删除避免一次性删几十万行长时间锁表
    const res: { affected?: number } = await repo.query(
      `DELETE FROM \`${table}\` WHERE created_at < ? LIMIT ?`,
      [cutoff, batchSize]
    );
    const affected = res?.affected ?? 0;
    total += affected;
    if (affected < batchSize) break; // 已删完
    await new Promise((r) => setTimeout(r, 50)); // 让出连接与表锁
  }
  return total;
}

/**
 * 清理超过保留天数的操作日志，返回删除条数。
 * @param retentionDays 保留天数（<=0 时使用默认 1 天）
 */
export async function cleanExpiredLogs(retentionDays?: number): Promise<number> {
  return deleteExpiredInBatches(
    AppDataSource.getRepository(OperationLog),
    retentionCutoff(retentionDays)
  );
}

/**
 * 清理超过保留天数的心跳日志（设备每 6 小时上报一条，量大需纳入自动清理避免爆盘）。
 * @param retentionDays 保留天数（<=0 时使用默认 1 天）
 */
export async function cleanExpiredHeartbeats(retentionDays?: number): Promise<number> {
  return deleteExpiredInBatches(
    AppDataSource.getRepository(Heartbeat),
    retentionCutoff(retentionDays)
  );
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
