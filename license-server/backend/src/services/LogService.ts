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
