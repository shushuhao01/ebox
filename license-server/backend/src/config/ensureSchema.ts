import { AppDataSource } from './database';
import { log } from './logger';

/**
 * 启动时结构自愈：仅针对官网访问明细表 site_visit_log。
 * - 表不存在 → 自动创建（含索引）
 * - 表已存在但字段不全 → 自动补齐缺失字段
 * 其余表结构仍由 database/schema.sql 统一管理（synchronize: false 不变）。
 */

const TABLE = 'site_visit_log';

/** 表内除主键外的全部字段定义（用于表已存在时补列） */
const COLUMNS: Array<{ name: string; ddl: string }> = [
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
CREATE TABLE IF NOT EXISTS \`${TABLE}\` (
  \`id\` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
${COLUMNS.map((c) => `  \`${c.name}\` ${c.ddl}`).join(',\n')},
  KEY \`idx_svl_date\` (\`visit_date\`),
  KEY \`idx_svl_ip\` (\`ip\`),
  KEY \`idx_svl_path\` (\`path\`),
  KEY \`idx_svl_visitor\` (\`visitor_id\`),
  KEY \`idx_svl_created\` (\`created_at\`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4`;

/** 幂等同步官网访问明细表结构，失败仅告警不阻断启动 */
export async function ensureSchema(): Promise<void> {
  const runner = AppDataSource.createQueryRunner();
  try {
    await runner.query(CREATE_SQL);

    const rows: Array<{ name: string }> = await runner.query(
      'SELECT COLUMN_NAME AS name FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ?',
      [TABLE]
    );
    const existing = new Set(rows.map((r) => r.name));
    const missing = COLUMNS.filter((c) => !existing.has(c.name));

    for (const col of missing) {
      await runner.query(`ALTER TABLE \`${TABLE}\` ADD COLUMN \`${col.name}\` ${col.ddl}`);
    }
    if (missing.length) {
      log.info(`✅ site_visit_log 表结构已同步（自动补齐 ${missing.length} 个字段：${missing.map((c) => c.name).join(', ')}）`);
    }
  } catch (e) {
    log.error('site_visit_log 表结构自动同步失败（请检查数据库权限或手动执行 schema.sql）', e);
  } finally {
    await runner.release();
  }
}
