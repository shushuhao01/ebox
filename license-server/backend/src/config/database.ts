import 'reflect-metadata';
import { DataSource } from 'typeorm';
import { env } from './env';
import { User } from '../entities/User';
import { LicenseKey } from '../entities/LicenseKey';
import { Customer } from '../entities/Customer';
import { KeyCustomer } from '../entities/KeyCustomer';
import { Device } from '../entities/Device';
import { Heartbeat } from '../entities/Heartbeat';
import { UnbindLog } from '../entities/UnbindLog';
import { RevokeLog } from '../entities/RevokeLog';
import { OperationLog } from '../entities/OperationLog';
import { SystemConfig } from '../entities/SystemConfig';
import { KeyBatch } from '../entities/KeyBatch';

export const AppDataSource = new DataSource({
  type: 'mysql',
  host: env.db.host,
  port: env.db.port,
  username: env.db.user,
  password: env.db.password,
  database: env.db.database,
  charset: 'utf8mb4',
  entities: [
    User, LicenseKey, Customer, KeyCustomer, Device,
    Heartbeat, UnbindLog, RevokeLog, OperationLog, SystemConfig, KeyBatch,
  ],
  synchronize: false, // 表结构由 database/schema.sql 管理，禁止自动同步
  logging: false,
  timezone: '+08:00',
  connectTimeout: 10000,
  maxQueryExecutionTime: 5000,
});

export async function initializeDatabase(): Promise<void> {
  await AppDataSource.initialize();
}
