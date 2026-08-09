import { LessThan } from 'typeorm';
import { AppDataSource } from '../config/database';
import { LicenseKey } from '../entities/LicenseKey';
import { Device } from '../entities/Device';
import { Heartbeat } from '../entities/Heartbeat';
import { UnbindLog } from '../entities/UnbindLog';
import { RevokeLog } from '../entities/RevokeLog';
import { KeyCustomer } from '../entities/KeyCustomer';
import { KeyBatch } from '../entities/KeyBatch';
import { buildLicenseCode, decodeAndVerify, codeFingerprint } from '../crypto/licenseCodec';
import { getConfigValue } from './ConfigService';
import { ApiError } from '../middleware/errorHandler';

const keyRepo = () => AppDataSource.getRepository(LicenseKey);
const deviceRepo = () => AppDataSource.getRepository(Device);
const hbRepo = () => AppDataSource.getRepository(Heartbeat);
const unbindRepo = () => AppDataSource.getRepository(UnbindLog);

const nowSec = () => Math.floor(Date.now() / 1000);
const monthKey = (d: Date = new Date()) =>
  `${d.getFullYear()}${String(d.getMonth() + 1).padStart(2, '0')}`;

// ---------------------------------------------------------------------------
// 面板生成激活码（库存码，时长制）
// ---------------------------------------------------------------------------
export interface GenerateParams {
  durationSec: number; // 0=永久
  bound: boolean;
  unbindMax: number; // -1=不限 0=禁止 n=每月n次
  count: number; // 1..500
  customerId?: string | null;
  batchId?: string | null; // 归入已有批次（优先）
  batchName?: string | null; // 无 batchId 时按名称新建批次
  remark?: string | null;
  createdBy: string;
}

export async function generateKeys(p: GenerateParams): Promise<{ codes: string[]; batchId: string | null }> {
  let batchId: string | null = null;
  const batchRepo = AppDataSource.getRepository(KeyBatch);
  if (p.batchId) {
    // 归入已有批次
    const existing = await batchRepo.findOneBy({ id: p.batchId });
    if (existing) batchId = existing.id;
  }
  if (!batchId && p.batchName) {
    const batch = await batchRepo.save(
      batchRepo.create({ name: p.batchName, remark: p.remark, createdBy: p.createdBy })
    );
    batchId = batch.id;
  }
  const codes: string[] = [];
  // 批量插入：一次 INSERT 全部行，避免逐条 save 在大批量（如 500 个）时多次数据库往返导致请求超时
  const rows: Partial<LicenseKey>[] = [];
  for (let i = 0; i < p.count; i++) {
    const code = buildLicenseCode(p.durationSec, p.bound, p.unbindMax, true);
    if (!code) throw new ApiError('生成失败：私钥未配置', 500);
    rows.push({
      code,
      codeFp: codeFingerprint(code),
      type: 1,
      durationSec: String(p.durationSec),
      expireAt: null,
      bound: p.bound ? 1 : 0,
      unbindMax: p.bound ? p.unbindMax : 0,
      status: 0,
      batchId,
      customerId: p.customerId || null,
      remark: p.remark || null,
      createdBy: p.createdBy,
      createdAt: new Date(),
    });
    codes.push(code);
  }
  const insert = await keyRepo()
    .createQueryBuilder()
    .insert()
    .values(rows as never[])
    .execute();
  if (p.customerId) {
    // 客户关联同样批量插入（MySQL 单条多值 INSERT 的 identifiers 与 values 顺序一致）
    const kcRows = insert.identifiers.map((ident, idx) => ({ keyId: ident.id, customerId: p.customerId }));
    await AppDataSource.getRepository(KeyCustomer)
      .createQueryBuilder()
      .insert()
      .values(kcRows as never[])
      .execute();
  }
  return { codes, batchId };
}

// ---------------------------------------------------------------------------
// 按码文本查询（code_fp 索引加速）
// ---------------------------------------------------------------------------
export async function findByCode(code: string): Promise<LicenseKey | null> {
  return keyRepo().findOneBy({ codeFp: codeFingerprint(code) });
}

export async function findByCodeRaw(rawCode: string): Promise<LicenseKey | null> {
  return keyRepo().findOneBy({ code: rawCode });
}

// ---------------------------------------------------------------------------
// 客户端激活
// 返回 online=false 表示"未登记码，宽容放行（仅统计）"
// ---------------------------------------------------------------------------
export interface ActivateResult {
  online: boolean;
  expireType: 'duration' | 'absolute' | 'permanent';
  expireAt: number; // 绝对到期（换机码）；时长制按服务端激活时间+durationSec
  durationSec: number;
  bound: boolean;
  unbindMax: number;
  serverTime: number;
  graceUntil: number; // 离线可用到该时刻（=授权真实到期），激活后即使离线也能用完整个授权期限
  revoked?: boolean;
  expired?: boolean;
  exceeded?: boolean; // 绑定码已有其他设备在线
  heartbeatIntervalHours: number; // 心跳间隔（小时），客户端以此同步并写入本地注册表
  forceOnlineActivate: boolean;   // 是否强制在线激活，客户端以此同步并写入本地注册表
}

/** 授权真实到期时间（与客户端 expireTime 一致）：换机码=绝对到期；时长制=首次激活+时长；永久=MAX */
function keyExpireAt(key: LicenseKey, now: number): number {
  if (key.type === 2) return Number(key.expireAt || 0);
  if (key.durationSec === '0') return Number.MAX_SAFE_INTEGER;
  return (key.usedAt ? Math.floor(key.usedAt.getTime() / 1000) : now) + Number(key.durationSec || 0);
}

/** 未登记有效码的到期时间（纯离线码，按载荷计算） */
function parsedExpireAt(parsed: NonNullable<ReturnType<typeof decodeAndVerify>>, now: number): number {
  if (parsed.isDurationFormat) return parsed.durationSec > 0 ? now + parsed.durationSec : Number.MAX_SAFE_INTEGER;
  return parsed.expire;
}

export async function clientActivate(
  code: string,
  machineFp: string,
  opts: { ip?: string; os?: string; appVersion?: string; clientTime?: number }
): Promise<ActivateResult> {
  const parsed = decodeAndVerify(code);
  if (!parsed) throw new ApiError('激活码无效', 1001);

  const key = await findByCode(code);
  const serverTime = nowSec();

  if (!key) {
    // 未登记但签名有效：离线码宽容放行
    const expireAt = parsedExpireAt(parsed, serverTime);
    return {
      online: false,
      expireType: parsed.isDurationFormat ? (parsed.durationSec === 0 ? 'permanent' : 'duration') : 'absolute',
      expireAt,
      durationSec: parsed.durationSec,
      bound: parsed.bound,
      unbindMax: parsed.unbindMax,
      serverTime,
      graceUntil: expireAt,
      heartbeatIntervalHours: 6,
      forceOnlineActivate: false,
    };
  }

  // 已登记码：状态校验（5=已删除，视同作废，客户端保持锁定）
  if (key.status === 2 || key.status === 4 || key.status === 5)
    return { ...unregisteredActivate(key), revoked: true, online: true };
  if (key.status === 3 || (key.expireAt && Number(key.expireAt) < serverTime))
    return { ...unregisteredActivate(key), expired: true, online: true };

  // 绑定码：仅允许 1 台在线设备（status=1 在绑设备数 >=1 时拒绝新指纹）
  const activeDevices = await deviceRepo().findBy({ keyId: key.id, status: 1 });
  const existing = activeDevices.find((d) => d.machineFp === machineFp);
  if (key.bound === 1 && !existing && activeDevices.length >= 1) {
    return { ...unregisteredActivate(key), exceeded: true, online: true };
  }

  // 登记/更新设备
  let device: Device;
  if (existing) {
    device = existing;
    device.lastOnlineAt = new Date();
    device.lastHeartbeatAt = new Date();
    device.lastIp = opts.ip || null;
    device.osInfo = opts.os || null;
    device.appVersion = opts.appVersion || null;
    device.status = 1;
  } else {
    device = deviceRepo().create({
      keyId: key.id,
      machineFp,
      status: 1,
      firstActivateAt: new Date(),
      lastOnlineAt: new Date(),
      lastHeartbeatAt: new Date(),
      lastIp: opts.ip || null,
      osInfo: opts.os || null,
      appVersion: opts.appVersion || null,
    });
  }
  await deviceRepo().save(device);

  // 更新码状态
  if (key.status === 0) {
    key.status = 1;
    key.usedAt = new Date();
  }
  await keyRepo().save(key);

  // 心跳流水
  await hbRepo().save(
    hbRepo().create({
      keyId: key.id,
      deviceId: device.id,
      action: 1,
      clientTime: String(opts.clientTime || serverTime),
      ip: opts.ip || null,
      appVersion: opts.appVersion || null,
      detail: '客户端在线激活成功',
    })
  );

  // 时长制：到期 = 客户首次激活时间 + 有效时长（key.usedAt 为首次激活，重复激活不重置；与客户端一致）
  const expireAt = keyExpireAt(key, serverTime);
  // 授权策略随激活响应下发，客户端同步到本地注册表后生效
  const heartbeatIntervalHours = Number(await getConfigValue('heartbeat_interval_hours')) || 6;
  const forceOnlineActivate = (await getConfigValue('force_online_activate')) === '1';

  return {
    online: true,
    expireType: key.type === 2 ? 'absolute' : key.durationSec === '0' ? 'permanent' : 'duration',
    expireAt,
    durationSec: Number(key.durationSec || 0),
    bound: key.bound === 1,
    unbindMax: key.unbindMax,
    serverTime,
    graceUntil: expireAt,
    heartbeatIntervalHours,
    forceOnlineActivate,
  };
}

function unregisteredActivate(key: LicenseKey): ActivateResult {
  const expireAt = keyExpireAt(key, nowSec());
  return {
    online: false,
    expireType: key.type === 2 ? 'absolute' : key.durationSec === '0' ? 'permanent' : 'duration' as const,
    expireAt,
    durationSec: Number(key.durationSec || 0),
    bound: key.bound === 1,
    unbindMax: key.unbindMax,
    serverTime: nowSec(),
    graceUntil: expireAt,
    heartbeatIntervalHours: 6,
    forceOnlineActivate: false,
  };
}

// ---------------------------------------------------------------------------
// 客户端心跳
// ---------------------------------------------------------------------------
export interface HeartbeatResult {
  status: 'ok' | 'revoked' | 'expired' | 'kicked';
  online: boolean;
  graceUntil: number; // 服务端时间 + 宽限天数
  serverTime: number;
  notice?: string;
  heartbeatIntervalHours?: number; // 授权策略：心跳间隔（小时），客户端同步到本地注册表
  forceOnlineActivate?: boolean;   // 授权策略：是否强制在线激活，客户端同步到本地注册表
}

export async function clientHeartbeat(
  code: string,
  machineFp: string,
  opts: { ip?: string; appVersion?: string; clientTime?: number }
): Promise<HeartbeatResult> {
  const parsed = decodeAndVerify(code);
  if (!parsed) throw new ApiError('激活码无效', 1001);
  const key = await findByCode(code);
  const serverTime = nowSec();

  if (!key) {
    // 未登记码（纯离线码）：服务器不托管，graceUntil 按载荷到期时间下发
    return { status: 'ok', online: false, graceUntil: parsedExpireAt(parsed, serverTime), serverTime };
  }

  if (key.status === 2 || key.status === 4 || key.status === 5) {
    await hbRepo().save(hbRepo().create({ keyId: key.id, action: 2, clientTime: String(opts.clientTime || serverTime), ip: opts.ip || null, appVersion: opts.appVersion || null, detail: '心跳检测到激活码已被作废，本机授权已锁定' }));
    return { status: 'revoked', online: true, graceUntil: 0, serverTime };
  }
  if (key.status === 3 || (key.expireAt && Number(key.expireAt) < serverTime)) {
    await hbRepo().save(hbRepo().create({ keyId: key.id, action: 2, clientTime: String(opts.clientTime || serverTime), ip: opts.ip || null, appVersion: opts.appVersion || null, detail: '心跳检测到激活码已过期，本机授权已锁定' }));
    return { status: 'expired', online: true, graceUntil: 0, serverTime };
  }

  // 更新设备在线状态（若该设备存在）
  const device = await deviceRepo().findOneBy({ keyId: key.id, machineFp });
  if (device) {
    if (device.status === 2) {
      await hbRepo().save(hbRepo().create({ keyId: key.id, deviceId: device.id, action: 2, clientTime: String(opts.clientTime || serverTime), ip: opts.ip || null, appVersion: opts.appVersion || null, detail: '心跳检测到激活码被其他设备强制下线，本机授权已锁定' }));
      return { status: 'kicked', online: true, graceUntil: 0, serverTime };
    }
    device.lastOnlineAt = new Date();
    device.lastHeartbeatAt = new Date();
    device.lastIp = opts.ip || null;
    device.appVersion = opts.appVersion || null;
    await deviceRepo().save(device);
  }

  await hbRepo().save(
    hbRepo().create({
      keyId: key.id,
      deviceId: device?.id || null,
      action: 2,
      clientTime: String(opts.clientTime || serverTime),
      ip: opts.ip || null,
      appVersion: opts.appVersion || null,
      detail: '客户端心跳上报，在线状态正常',
    })
  );

  const notice = await getConfigValue('notice');
  // 授权策略随心跳下发：客户端同步到本地注册表（在线码；未登记离线码用本地默认）
  const heartbeatIntervalHours = Number(await getConfigValue('heartbeat_interval_hours')) || 6;
  const forceOnlineActivate = (await getConfigValue('force_online_activate')) === '1';
  // graceUntil = 授权真实到期时间：激活后即使长时间离线，也能用完整个授权期限
  return { status: 'ok', online: true, graceUntil: keyExpireAt(key, serverTime), serverTime, notice: notice || undefined, heartbeatIntervalHours, forceOnlineActivate };
}

// ---------------------------------------------------------------------------
// 客户端解绑换机：服务端签发换机码
// ---------------------------------------------------------------------------
export interface UnbindResult {
  online: boolean;
  newCode?: string;
  reason?: string;
  exceed?: boolean;
  unbindCount: number;
  unbindMax: number;
}

export async function clientUnbind(
  code: string,
  machineFp: string,
  opts: { ip?: string; appVersion?: string; clientTime?: number }
): Promise<UnbindResult> {
  const parsed = decodeAndVerify(code);
  if (!parsed) throw new ApiError('激活码无效', 1001);
  const key = await findByCode(code);
  const serverTime = nowSec();

  if (!key) {
    // 未登记码：无在线换机服务，交给客户端本地逻辑
    return { online: false, reason: 'offline-code', unbindCount: 0, unbindMax: parsed.unbindMax };
  }
  if (!parsed.bound) throw new ApiError('非绑定码无需解绑', 1003);
  if (key.status === 2 || key.status === 4 || key.status === 5) throw new ApiError('该码已作废或已换机', 1004);
  if (key.unbindMax === 0) throw new ApiError('该码禁止解绑', 1005);

  const month = monthKey();
  const used = await unbindRepo().countBy({ keyId: key.id, month });
  if (key.unbindMax !== -1 && used >= key.unbindMax) {
    return { online: true, reason: 'exceeded', exceed: true, unbindCount: used, unbindMax: key.unbindMax };
  }

  // 计算剩余到期（服务端视角）：锚定激活码首次激活时间，跨设备/重复激活不重置
  const device = await deviceRepo().findOneBy({ keyId: key.id, machineFp });
  const activatedAt = key.usedAt
    ? Math.floor(key.usedAt.getTime() / 1000)
    : device?.firstActivateAt
      ? Math.floor(device.firstActivateAt.getTime() / 1000)
      : serverTime;

  let remainExpire: number;
  if (key.type === 2) {
    remainExpire = Number(key.expireAt || 0);
  } else if (key.durationSec === '0') {
    remainExpire = Number.MAX_SAFE_INTEGER;
  } else {
    remainExpire = activatedAt + Number(key.durationSec || 0);
  }

  // 服务端签发换机码（绝对到期格式 verMinor=7）
  const newCode = buildLicenseCode(remainExpire, parsed.bound, parsed.unbindMax, false);
  if (!newCode) throw new ApiError('换机码签发失败：私钥未配置', 500);

  const newKey = await keyRepo().save(
    keyRepo().create({
      code: newCode,
      codeFp: codeFingerprint(newCode),
      type: 2,
      durationSec: '0',
      expireAt: String(remainExpire),
      bound: parsed.bound ? 1 : 0,
      unbindMax: parsed.unbindMax,
      status: 0,
      createdBy: key.createdBy,
      customerId: key.customerId,
      remark: key.remark,
      convertFromKeyId: key.id,
    })
  );

  // 旧码置"已换机"，旧设备解绑
  key.status = 4;
  await keyRepo().save(key);
  if (device) {
    device.status = 0;
    device.unbindAt = new Date();
    await deviceRepo().save(device);
  }

  await unbindRepo().save(
    unbindRepo().create({ keyId: key.id, deviceId: device?.id || '0', month, newKeyId: newKey.id })
  );
  await hbRepo().save(
    hbRepo().create({
      keyId: key.id,
      deviceId: device?.id || null,
      action: 3,
      clientTime: String(opts.clientTime || serverTime),
      ip: opts.ip || null,
      appVersion: opts.appVersion || null,
      detail: `解绑换机，服务端已签发新激活码 ${newCode.slice(0, 14)}...`,
    })
  );

  return { online: true, newCode, unbindCount: used + 1, unbindMax: key.unbindMax };
}

// ---------------------------------------------------------------------------
// 面板：作废 / 恢复 / 服务端签发换机码
// ---------------------------------------------------------------------------
export async function revokeKey(keyId: string, operatorId: string, reason: string): Promise<LicenseKey> {
  const key = await keyRepo().findOneBy({ id: keyId });
  if (!key) throw new ApiError('激活码不存在', 1002);
  if (key.status === 2) throw new ApiError('该码已是作废状态', 1004);
  if (key.status === 5) throw new ApiError('该码已删除，不可作废', 1004);
  key.status = 2;
  key.revokedAt = new Date();
  key.revokedBy = operatorId;
  key.revokedReason = reason || null;
  await keyRepo().save(key);
  await AppDataSource.getRepository(RevokeLog).save(
    AppDataSource.getRepository(RevokeLog).create({ keyId: key.id, operatorId, reason: reason || null })
  );
  return key;
}

export async function restoreKey(keyId: string): Promise<LicenseKey> {
  const key = await keyRepo().findOneBy({ id: keyId });
  if (!key) throw new ApiError('激活码不存在', 1002);
  if (key.status !== 2) throw new ApiError('仅作废状态的码可恢复', 1004);
  key.status = 0;
  key.revokedAt = null;
  key.revokedBy = null;
  key.revokedReason = null;
  await keyRepo().save(key);
  return key;
}

/** 回收站恢复：已删除(5) → 作废(2)，保留原作废信息 */
export async function restoreDeletedKey(keyId: string): Promise<LicenseKey> {
  const key = await keyRepo().findOneBy({ id: keyId });
  if (!key) throw new ApiError('激活码不存在', 1002);
  if (key.status !== 5) throw new ApiError('仅回收站中的激活码可恢复', 1004);
  key.status = 2;
  await keyRepo().save(key);
  return key;
}

// 面板 convert：对已用库存码签发换机码（续期场景，继承剩余时间）
export async function convertToSwitchKey(keyId: string, operatorId: string): Promise<LicenseKey> {
  const key = await keyRepo().findOneBy({ id: keyId });
  if (!key) throw new ApiError('激活码不存在', 1002);
  if (key.status !== 1) throw new ApiError('仅已用状态的码可换机', 1004);

  const device = await deviceRepo().findOneBy({ keyId: key.id, status: 1 });
  const activatedAt = key.usedAt
    ? Math.floor(key.usedAt.getTime() / 1000)
    : device?.firstActivateAt
      ? Math.floor(device.firstActivateAt.getTime() / 1000)
      : nowSec();
  const remainExpire =
    key.type === 2
      ? Number(key.expireAt || 0)
      : key.durationSec === '0'
        ? Number.MAX_SAFE_INTEGER
        : activatedAt + Number(key.durationSec || 0);

  const newCode = buildLicenseCode(remainExpire, key.bound === 1, key.unbindMax, false);
  if (!newCode) throw new ApiError('换机码签发失败：私钥未配置', 500);

  const newKey = await keyRepo().save(
    keyRepo().create({
      code: newCode,
      codeFp: codeFingerprint(newCode),
      type: 2,
      durationSec: '0',
      expireAt: String(remainExpire),
      bound: key.bound,
      unbindMax: key.unbindMax,
      status: 0,
      createdBy: operatorId,
      customerId: key.customerId,
      remark: key.remark,
      convertFromKeyId: key.id,
    })
  );
  key.status = 4;
  await keyRepo().save(key);
  if (device) {
    device.status = 0;
    device.unbindAt = new Date();
    await deviceRepo().save(device);
  }
  const month = monthKey();
  await unbindRepo().save(
    unbindRepo().create({ keyId: key.id, deviceId: device?.id || '0', month, newKeyId: newKey.id })
  );
  return newKey;
}

// ---------------------------------------------------------------------------
// 面板查询辅助
// ---------------------------------------------------------------------------
export async function keyWithDetail(id: string) {
  return keyRepo().findOneBy({ id });
}

export async function countUnbindThisMonth(keyId: string): Promise<number> {
  return unbindRepo().countBy({ keyId, month: monthKey() });
}

// 清理过期状态（cron）：把绝对到期已过期的换机码置为"过期"
export async function markExpiredKeys(): Promise<number> {
  const now = nowSec();
  const keys = await keyRepo().findBy({ status: 1, type: 2 });
  let n = 0;
  for (const k of keys) {
    if (k.expireAt && Number(k.expireAt) < now) {
      k.status = 3;
      await keyRepo().save(k);
      n++;
    }
  }
  return n;
}
