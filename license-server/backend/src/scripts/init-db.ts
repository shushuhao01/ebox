// 初始化数据库（建库 + 导入 schema.sql + 校验连接）
// 用法：npm run init:db
// 前提：MySQL 已启动；若 schema 未导入，请先执行：
//   mysql -u root -p < database/schema.sql
import { loadEnv } from './loadEnv';
loadEnv();

import 'reflect-metadata';
import { AppDataSource } from '../config/database';
import { SystemConfig } from '../entities/SystemConfig';

async function main() {
  await AppDataSource.initialize();
  const repo = AppDataSource.getRepository(SystemConfig);
  const count = await repo.count();
  console.log(`✅ 数据库连接成功：${process.env.DB_DATABASE || 'license_server'}`);
  console.log(`   系统配置表记录数：${count}`);
  console.log('   若表不存在，请先执行 schema.sql（见 database/schema.sql）');
  await AppDataSource.destroy();
}

main().catch((e) => {
  console.error('数据库初始化失败：', (e as Error).message);
  console.log('   1) 确认 MySQL 已启动');
  console.log('   2) 确认 .env 中 DB_* 配置正确');
  console.log('   3) 首次部署请先导入 database/schema.sql');
  process.exit(1);
});
