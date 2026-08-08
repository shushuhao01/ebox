import { AppDataSource } from '../config/database';
import { SystemConfig } from '../entities/SystemConfig';

const DEFAULTS: Record<string, string> = {
  heartbeat_interval_hours: '6',
  offline_grace_days: '7',
  force_online_activate: '0',
  notice: '',
  online_threshold_minutes: '30',
};

const repo = () => AppDataSource.getRepository(SystemConfig);

export async function getConfigValue(key: string): Promise<string> {
  const row = await repo().findOneBy({ cfgKey: key });
  return row?.cfgValue ?? DEFAULTS[key] ?? '';
}

export async function getConfigAll(): Promise<Record<string, string>> {
  const rows = await repo().find();
  const map: Record<string, string> = { ...DEFAULTS };
  for (const r of rows) map[r.cfgKey] = r.cfgValue;
  return map;
}

export async function setConfigValue(key: string, value: string): Promise<void> {
  let row = await repo().findOneBy({ cfgKey: key });
  if (!row) {
    row = repo().create({ cfgKey: key, cfgValue: value });
  } else {
    row.cfgValue = value;
  }
  await repo().save(row);
}
