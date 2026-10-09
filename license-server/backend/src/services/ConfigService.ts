import { AppDataSource } from '../config/database';
import { SystemConfig } from '../entities/SystemConfig';
import { broadcastNotice } from './NoticeBroadcaster';

const DEFAULTS: Record<string, string> = {
  heartbeat_interval_hours: '6',
  offline_grace_days: '7',
  force_online_activate: '0',
  notice: '',
  online_threshold_minutes: '30',
  // 操作日志自动清理：保留天数（默认 1 天=24 小时），每日清理时间 HH:mm
  log_retention_days: '1',
  log_clean_time: '03:00',
};

const repo = () => AppDataSource.getRepository(SystemConfig);

export async function getConfigValue(key: string): Promise<string> {
  const row = await repo().findOneBy({ cfgKey: key });
  return row?.cfgValue ?? DEFAULTS[key] ?? '';
}

/**
 * 在线判定窗口（分钟）= 心跳间隔 + 宽限阈值。
 * 客户端按 heartbeat_interval_hours 周期上报心跳，若判定窗口小于一个心跳周期，
 * 心跳间隙内的正常在线设备会被误判为离线，导致"在线设备"数量长期偏低。
 * 因此窗口必须不小于一个心跳周期，online_threshold_minutes 作为漏报一次的宽限。
 */
export async function getOnlineWindowMinutes(): Promise<number> {
  const intervalHours = Number(await getConfigValue('heartbeat_interval_hours')) || 6;
  const graceMin = parseInt(await getConfigValue('online_threshold_minutes'), 10) || 30;
  return intervalHours * 60 + graceMin;
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
  // 公告变更：向所有在线客户端实时广播（SSE）；心跳下发仍作兜底
  if (key === 'notice') {
    broadcastNotice(value);
  }
}
