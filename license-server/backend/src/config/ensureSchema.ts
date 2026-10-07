import type { QueryRunner } from 'typeorm';
import { AppDataSource } from './database';
import { log } from './logger';

/**
 * 启动时结构自愈：
 * - site_visit_log 表不存在 → 自动创建（含索引）；字段不全 → 自动补齐
 * - site_articles 表已存在但字段不全 → 自动补齐（如 expire_at / link_url）
 * 其余表结构仍由 database/schema.sql 统一管理（synchronize: false 不变）。
 */

const VISIT_TABLE = 'site_visit_log';

/** 表内除主键外的全部字段定义（用于表已存在时补列） */
const VISIT_COLUMNS: Array<{ name: string; ddl: string }> = [
  { name: 'visit_date', ddl: 'DATE NOT NULL' },
  { name: 'visit_hour', ddl: 'TINYINT UNSIGNED NOT NULL DEFAULT 0' },
  { name: 'ip', ddl: "VARCHAR(64) NOT NULL DEFAULT ''" },
  { name: 'country', ddl: "VARCHAR(16) NOT NULL DEFAULT ''" },
  { name: 'province', ddl: "VARCHAR(32) NOT NULL DEFAULT ''" },
  { name: 'city', ddl: "VARCHAR(32) NOT NULL DEFAULT ''" },
  { name: 'isp', ddl: "VARCHAR(64) NOT NULL DEFAULT ''" },
  { name: 'device', ddl: "VARCHAR(16) NOT NULL DEFAULT 'unknown'" },
  { name: 'os', ddl: "VARCHAR(32) NOT NULL DEFAULT ''" },
  { name: 'browser', ddl: "VARCHAR(32) NOT NULL DEFAULT ''" },
  { name: 'source', ddl: "VARCHAR(24) NOT NULL DEFAULT '其他'" },
  { name: 'referer', ddl: "VARCHAR(512) NOT NULL DEFAULT ''" },
  { name: 'path', ddl: "VARCHAR(255) NOT NULL DEFAULT '/'" },
  { name: 'visitor_id', ddl: "VARCHAR(64) NOT NULL DEFAULT ''" },
  { name: 'user_agent', ddl: "VARCHAR(255) NOT NULL DEFAULT ''" },
  { name: 'created_at', ddl: 'DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP' },
];

const CREATE_SQL = `
CREATE TABLE IF NOT EXISTS \`${VISIT_TABLE}\` (
  \`id\` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
${VISIT_COLUMNS.map((c) => `  \`${c.name}\` ${c.ddl}`).join(',\n')},
  KEY \`idx_svl_date\` (\`visit_date\`),
  KEY \`idx_svl_ip\` (\`ip\`),
  KEY \`idx_svl_path\` (\`path\`),
  KEY \`idx_svl_visitor\` (\`visitor_id\`),
  KEY \`idx_svl_created\` (\`created_at\`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4`;

/** 需要幂等补列的表（表结构由 schema.sql 创建，此处仅补缺失字段） */
const PATCH_TABLES: Array<{ table: string; columns: Array<{ name: string; ddl: string }> }> = [
  {
    table: 'site_articles',
    columns: [
      { name: 'expire_at', ddl: 'DATETIME NULL DEFAULT NULL' },
      { name: 'link_url', ddl: 'VARCHAR(500) NULL DEFAULT NULL' },
    ],
  },
];

/** 补齐单表缺失字段，返回补列数量 */
async function ensureColumns(
  runner: QueryRunner,
  table: string,
  columns: Array<{ name: string; ddl: string }>
): Promise<number> {
  const rows: Array<{ name: string }> = await runner.query(
    'SELECT COLUMN_NAME AS name FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ?',
    [table]
  );
  const existing = new Set(rows.map((r) => r.name));
  const missing = columns.filter((c) => !existing.has(c.name));
  for (const col of missing) {
    await runner.query(`ALTER TABLE \`${table}\` ADD COLUMN \`${col.name}\` ${col.ddl}`);
  }
  if (missing.length) {
    log.info(`✅ ${table} 表结构已同步（自动补齐 ${missing.length} 个字段：${missing.map((c) => c.name).join(', ')}）`);
  }
  return missing.length;
}

/** 幂等同步表结构，失败仅告警不阻断启动 */
export async function ensureSchema(): Promise<void> {
  const runner = AppDataSource.createQueryRunner();
  try {
    await runner.query(CREATE_SQL);
    await ensureColumns(runner, VISIT_TABLE, VISIT_COLUMNS);
    for (const t of PATCH_TABLES) {
      await ensureColumns(runner, t.table, t.columns);
    }
  } catch (e) {
    log.error('表结构自动同步失败（请检查数据库权限或手动执行 schema.sql）', e);
  } finally {
    await runner.release();
  }
}
