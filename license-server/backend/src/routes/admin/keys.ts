import { Router } from 'express';
import Joi from 'joi';
import { Between, Like, In } from 'typeorm';
import { AppDataSource } from '../../config/database';
import { LicenseKey } from '../../entities/LicenseKey';
import { Device } from '../../entities/Device';
import { Customer } from '../../entities/Customer';
import { KeyBatch } from '../../entities/KeyBatch';
import { ok, fail, clientIp } from '../../middleware/helpers';
import { ApiError } from '../../middleware/errorHandler';
import { generateKeys, revokeKey, restoreKey, restoreDeletedKey, convertToSwitchKey, findByCode, countUnbindThisMonth } from '../../services/KeyService';
import { writeOperationLog } from '../../services/LogService';
import { decodeAndVerify, normalizeCode } from '../../crypto/licenseCodec';

const router = Router();
const keyRepo = () => AppDataSource.getRepository(LicenseKey);

// 激活码状态文案（错误提示用）
const STATUS_TEXT: Record<number, string> = { 0: '未用', 1: '已用', 2: '作废', 3: '过期', 4: '已换机', 5: '已删除' };

// POST /api/admin/keys/generate  生成激活码（批量）
router.post('/keys/generate', async (req, res) => {
  try {
    const { error, value } = Joi.object({
      durationSec: Joi.number().integer().min(0).required(),
      bound: Joi.boolean().required(),
      unbindMax: Joi.number().integer().min(-1).max(255).default(3),
      count: Joi.number().integer().min(1).max(500).default(1),
      customerId: Joi.string().allow('', null).default(null),
      batchId: Joi.string().allow('', null).max(64).default(null),
      batchName: Joi.string().allow('', null).max(64).default(null),
      remark: Joi.string().allow('', null).max(255).default(null),
    }).validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);

    const { codes, batchId } = await generateKeys({ ...value, createdBy: req.auth!.userId });
    const durationText = value.durationSec === 0 ? '永久' : `${Math.floor(value.durationSec / 86400)}天`;
    const unbindText = !value.bound ? '-' : value.unbindMax < 0 ? '不限' : value.unbindMax === 0 ? '禁止' : `每月${value.unbindMax}次`;
    const batchText = value.batchName || (batchId ? `批次#${batchId}` : '');
    // 目标优先显示批次名；无批次时显示首个激活码，保证操作日志"目标"列始终有内容
    const target = batchText || (codes.length > 0 ? `${codes[0]} 等${codes.length}个` : null);
    await writeOperationLog(
      req.auth!.userId,
      '生成激活码',
      target,
      `数量=${value.count} 时长=${durationText} 绑定=${value.bound ? '绑定' : '通用'} 解绑上限=${unbindText}${value.customerId ? ` 客户ID=${value.customerId}` : ''}`,
      clientIp(req)
    );
    ok(res, { codes, batchId, count: codes.length });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// GET /api/admin/keys  分页+筛选+搜索
router.get('/keys', async (req, res) => {
  const page = Math.max(1, parseInt(String(req.query.page || '1'), 10) || 1);
  const pageSize = Math.min(100, Math.max(1, parseInt(String(req.query.pageSize || '20'), 10) || 20));
  const qb = keyRepo()
    .createQueryBuilder('k')
    .leftJoin(Customer, 'c', 'c.id = k.customer_id')
    .where('1=1');

  const status = String(req.query.status || '');
  if (status !== '' && status !== 'all') qb.andWhere('k.status = :s', { s: Number(status) });
  else qb.andWhere('k.status != 5'); // 默认隐藏「已删除」
  const type = String(req.query.type || '');
  if (type !== '' && type !== 'all') qb.andWhere('k.type = :t', { t: Number(type) });
  const bound = String(req.query.bound || '');
  if (bound === '1' || bound === '0') qb.andWhere('k.bound = :b', { b: Number(bound) });
  const batchId = String(req.query.batchId || '');
  if (batchId && batchId !== 'all') qb.andWhere('k.batch_id = :bid', { bid: batchId });
  const customerId = String(req.query.customerId || '');
  if (customerId && customerId !== 'all') qb.andWhere('k.customer_id = :cid', { cid: customerId });
  const start = String(req.query.start || '');
  const end = String(req.query.end || '');
  if (start && end) qb.andWhere('k.created_at >= :start AND k.created_at <= :end', { start, end: `${end} 23:59:59` });
  const search = String(req.query.search || '').trim();
  if (search) {
    qb.andWhere('(k.code LIKE :kw OR k.code_fp = :fp OR c.name LIKE :kw)', {
      kw: `%${search}%`,
      fp: search.length === 64 ? search : '',
    });
  }

  const total = await qb.getCount();
  const rows = await qb
    .orderBy('k.createdAt', 'DESC')
    .skip((page - 1) * pageSize)
    .take(pageSize)
    .getMany();

  ok(res, { total, page, pageSize, list: rows });
});

// POST /api/admin/keys/:id/revoke  作废
router.post('/keys/:id/revoke', async (req, res) => {
  try {
    const reason = String(req.body?.reason || '').slice(0, 255);
    const key = await revokeKey(req.params.id, req.auth!.userId, reason);
    await writeOperationLog(req.auth!.userId, '作废激活码', key.code, `原因：${reason || '无'}`, clientIp(req));
    ok(res, { id: key.id, code: key.code, status: key.status });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// POST /api/admin/keys/:id/restore  恢复
router.post('/keys/:id/restore', async (req, res) => {
  try {
    const key = await restoreKey(req.params.id);
    await writeOperationLog(req.auth!.userId, '恢复激活码', key.code, '撤销作废', clientIp(req));
    ok(res, { id: key.id, code: key.code, status: key.status });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// POST /api/admin/keys/:id/convert  服务端签发换机码
router.post('/keys/:id/convert', async (req, res) => {
  try {
    const newKey = await convertToSwitchKey(req.params.id, req.auth!.userId);
    const srcKey = newKey.convertFromKeyId ? await keyRepo().findOneBy({ id: newKey.convertFromKeyId }) : null;
    const expireText = newKey.expireAt
      ? new Date(Number(newKey.expireAt) * 1000).toLocaleString('zh-CN', { hour12: false })
      : '-';
    await writeOperationLog(req.auth!.userId, '签发换机码', srcKey?.code || `码#${req.params.id}`, `已签发换机码：${newKey.code} 到期=${expireText}`, clientIp(req));
    ok(res, { id: newKey.id, code: newKey.code, expireAt: newKey.expireAt });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// POST /api/admin/keys/verify  验一个码
router.post('/keys/verify', async (req, res) => {
  const code = String(req.body?.code || '');
  const parsed = decodeAndVerify(code);
  if (!parsed) return fail(res, '激活码无效（签名/格式错误）', 1001);
  const key = await findByCode(code);
  const unbindCount = key ? await countUnbindThisMonth(key.id) : 0;
  ok(res, {
    registered: !!key,
    ...parsed,
    status: key?.status ?? null,
    unbindCount,
  });
});

// GET /api/admin/keys/export  导出 CSV（筛选条件与列表一致）
router.get('/keys/export', async (req, res) => {
  const qb = keyRepo()
    .createQueryBuilder('k')
    .leftJoin(Customer, 'c', 'c.id = k.customer_id')
    .where('1=1');

  const status = String(req.query.status || '');
  if (status !== '' && status !== 'all') qb.andWhere('k.status = :s', { s: Number(status) });
  else qb.andWhere('k.status != 5'); // 默认隐藏「已删除」
  const type = String(req.query.type || '');
  if (type !== '' && type !== 'all') qb.andWhere('k.type = :t', { t: Number(type) });
  const bound = String(req.query.bound || '');
  if (bound === '1' || bound === '0') qb.andWhere('k.bound = :b', { b: Number(bound) });
  const batchId = String(req.query.batchId || '');
  if (batchId && batchId !== 'all') qb.andWhere('k.batch_id = :bid', { bid: batchId });
  const customerId = String(req.query.customerId || '');
  if (customerId && customerId !== 'all') qb.andWhere('k.customer_id = :cid', { cid: customerId });
  const start = String(req.query.start || '');
  const end = String(req.query.end || '');
  if (start && end) qb.andWhere('k.created_at >= :start AND k.created_at <= :end', { start, end: `${end} 23:59:59` });
  const search = String(req.query.search || '').trim();
  if (search) {
    qb.andWhere('(k.code LIKE :kw OR k.code_fp = :fp OR c.name LIKE :kw)', {
      kw: `%${search}%`,
      fp: search.length === 64 ? search : '',
    });
  }

  const rows = await qb.orderBy('k.createdAt', 'DESC').getMany();

  // 批量取客户名/批次名，避免逐行查询
  const batchRepo = AppDataSource.getRepository(KeyBatch);
  const customerRepo = AppDataSource.getRepository(Customer);
  const batchIds = Array.from(new Set(rows.map((r) => r.batchId).filter((v): v is string => !!v)));
  const customerIds = Array.from(new Set(rows.map((r) => r.customerId).filter((v): v is string => !!v)));
  const [batches, customers] = await Promise.all([
    batchIds.length ? batchRepo.findBy({ id: In(batchIds) }) : Promise.resolve([]),
    customerIds.length ? customerRepo.findBy({ id: In(customerIds) }) : Promise.resolve([]),
  ]);
  const batchNameMap = new Map(batches.map((b) => [String(b.id), b.name]));
  const customerNameMap = new Map(customers.map((c) => [String(c.id), c.name]));

  // CSV 字段转义：含逗号/引号/换行时双引号包裹并转义内部引号
  const esc = (v: unknown): string => {
    const s = v === null || v === undefined ? '' : String(v);
    return /[",\n\r]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
  };
  const fmtDateTime = (d: Date | string | null | undefined): string => {
    if (!d) return '';
    const date = typeof d === 'string' ? new Date(d) : d;
    if (Number.isNaN(date.getTime())) return String(d);
    const p = (n: number) => String(n).padStart(2, '0');
    return `${date.getFullYear()}-${p(date.getMonth() + 1)}-${p(date.getDate())} ${p(date.getHours())}:${p(date.getMinutes())}:${p(date.getSeconds())}`;
  };
  const fmtDuration = (sec: string | number): string => {
    const s = Number(sec ?? 0);
    if (!s) return '永久';
    const days = Math.floor(s / 86400);
    if (days >= 365 && days % 365 === 0) return `${days / 365}年`;
    return `${days}天`;
  };
  const fmtExpire = (v: string | null): string => {
    if (!v) return '';
    const n = Number(v);
    if (!Number.isFinite(n) || n <= 0) return '';
    const d = new Date(n * 1000);
    if (Number.isNaN(d.getTime())) return '';
    const p = (m: number) => String(m).padStart(2, '0');
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
  };
  const typeMap: Record<number, string> = { 1: '库存', 2: '换机' };
  const statusMap: Record<number, string> = { 0: '未用', 1: '已用', 2: '作废', 3: '过期', 4: '已换机', 5: '已删除' };
  const boundMap: Record<number, string> = { 0: '通用', 1: '绑定' };
  const unbindText = (n: number): string => (n < 0 ? '不限' : n === 0 ? '禁止' : `每月${n}次`);

  const header = ['激活码', '类型', '时长/到期', '绑定', '解绑上限', '状态', '客户', '批次', '备注', '首次激活', '创建时间'];
  const lines = [header.join(',')];
  for (const k of rows) {
    lines.push(
      [
        esc(k.code),
        esc(typeMap[k.type] ?? k.type),
        esc(k.type === 1 ? fmtDuration(k.durationSec) : fmtExpire(k.expireAt)),
        esc(boundMap[k.bound] ?? k.bound),
        esc(unbindText(k.unbindMax)),
        esc(statusMap[k.status] ?? k.status),
        esc(customerNameMap.get(String(k.customerId || '')) || k.customerId || ''),
        esc(batchNameMap.get(String(k.batchId || '')) || k.batchId || ''),
        esc(k.remark || ''),
        esc(fmtDateTime(k.usedAt)),
        esc(fmtDateTime(k.createdAt)),
      ].join(',')
    );
  }
  res.setHeader('Content-Type', 'text/csv; charset=utf-8');
  res.setHeader('Content-Disposition', `attachment; filename=keys_${new Date().toISOString().slice(0, 10)}.csv`);
  res.send('\ufeff' + lines.join('\r\n'));
});

// POST /api/admin/keys/batch-revoke  批量作废
router.post('/keys/batch-revoke', async (req, res) => {
  try {
    const { error, value } = Joi.object({
      ids: Joi.array().items(Joi.string().required()).min(1).max(500).required(),
      reason: Joi.string().allow('').max(255).default(''),
    }).validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);

    let okCount = 0;
    let failCount = 0;
    for (const id of value.ids) {
      try {
        await revokeKey(id, req.auth!.userId, value.reason);
        okCount++;
      } catch {
        failCount++;
      }
    }
    await writeOperationLog(
      req.auth!.userId,
      '批量作废激活码',
      null,
      `成功=${okCount} 失败=${failCount}${value.reason ? ` 原因：${value.reason}` : ''}`,
      clientIp(req)
    );
    if (okCount === 0) return fail(res, `全部作废失败（${failCount} 个，可能已作废/已删除）`, 1004);
    ok(res, { okCount, failCount });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// POST /api/admin/keys/batch-delete  批量删除（仅已作废的码，软删除 status=5）
router.post('/keys/batch-delete', async (req, res) => {
  try {
    const { error, value } = Joi.object({
      ids: Joi.array().items(Joi.string().required()).min(1).max(500).required(),
    }).validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);

    const keys = await keyRepo().findBy({ id: In(value.ids) });
    const notRevoked = keys.filter((k) => k.status !== 2);
    if (notRevoked.length) {
      const tails = notRevoked.slice(0, 5).map((k) => `…${k.code.slice(-6)}`).join('、');
      const more = notRevoked.length > 5 ? ` 等共 ${notRevoked.length} 个` : '';
      return fail(res, `存在 ${notRevoked.length} 个未作废的激活码，删除前必须先作废：${tails}${more}`, 1004);
    }
    for (const k of keys) {
      k.status = 5;
      await keyRepo().save(k);
    }
    await writeOperationLog(req.auth!.userId, '批量删除激活码', null, `删除=${keys.length} 个已作废激活码`, clientIp(req));
    ok(res, { deleted: keys.length });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// POST /api/admin/keys/batch-restore  回收站批量恢复（已删除 → 作废）
router.post('/keys/batch-restore', async (req, res) => {
  try {
    const { error, value } = Joi.object({
      ids: Joi.array().items(Joi.string().required()).min(1).max(500).required(),
    }).validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);

    const keys = await keyRepo().findBy({ id: In(value.ids) });
    const notDeleted = keys.filter((k) => k.status !== 5);
    if (notDeleted.length) {
      const tails = notDeleted.slice(0, 5).map((k) => `…${k.code.slice(-6)}`).join('、');
      const more = notDeleted.length > 5 ? ` 等共 ${notDeleted.length} 个` : '';
      return fail(res, `存在 ${notDeleted.length} 个不在回收站中的激活码：${tails}${more}`, 1004);
    }
    for (const k of keys) {
      await restoreDeletedKey(k.id);
    }
    await writeOperationLog(req.auth!.userId, '回收站恢复激活码', null, `恢复=${keys.length} 个（已删除 → 作废）`, clientIp(req));
    ok(res, { restored: keys.length });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// DELETE /api/admin/keys/:id  删除激活码（仅已作废，软删除 status=5）
router.delete('/keys/:id', async (req, res) => {
  try {
    const key = await keyRepo().findOneBy({ id: req.params.id });
    if (!key) return fail(res, '激活码不存在', 1002);
    if (key.status !== 2) return fail(res, `该激活码当前为「${STATUS_TEXT[key.status] ?? key.status}」，删除前必须先作废`, 1004);
    key.status = 5;
    await keyRepo().save(key);
    await writeOperationLog(req.auth!.userId, '删除激活码', key.code, `作废原因：${key.revokedReason || '无'}`, clientIp(req));
    ok(res, { id: key.id, code: key.code, status: key.status });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// GET /api/admin/keys/:id  详情（含设备）
router.get('/keys/:id', async (req, res) => {
  const key = await keyRepo().findOneBy({ id: req.params.id });
  if (!key) return fail(res, '激活码不存在', 1002);
  const devices = await AppDataSource.getRepository(Device).find({ where: { keyId: key.id }, order: { lastOnlineAt: 'DESC' } });
  const unbindCount = await countUnbindThisMonth(key.id);
  ok(res, { key, devices, unbindCount });
});

export default router;
