import { Router } from 'express';
import Joi from 'joi';
import { AppDataSource } from '../../config/database';
import { Customer } from '../../entities/Customer';
import { LicenseKey } from '../../entities/LicenseKey';
import { ok, fail, clientIp } from '../../middleware/helpers';
import { ApiError } from '../../middleware/errorHandler';
import { writeOperationLog } from '../../services/LogService';

const router = Router();
const repo = () => AppDataSource.getRepository(Customer);

// GET /api/admin/customers  分页
router.get('/customers', async (req, res) => {
  const page = Math.max(1, parseInt(String(req.query.page || '1'), 10) || 1);
  const pageSize = Math.min(100, Math.max(1, parseInt(String(req.query.pageSize || '20'), 10) || 20));
  const qb = repo().createQueryBuilder('c');
  const search = String(req.query.search || '').trim();
  if (search) qb.where('(c.name LIKE :kw OR c.phone LIKE :kw OR c.wechat LIKE :kw OR c.qq LIKE :kw)', { kw: `%${search}%` });
  const status = String(req.query.status || '');
  if (status !== '' && status !== 'all') qb.andWhere('c.status = :s', { s: Number(status) });
  const total = await qb.getCount();
  const rows = await qb.orderBy('c.createdAt', 'DESC').skip((page - 1) * pageSize).take(pageSize).getMany();

  // 附加名下码数与设备数
  const keyRepo = AppDataSource.getRepository(LicenseKey);
  const enriched = await Promise.all(
    rows.map(async (c) => {
      const keyCount = await keyRepo.countBy({ customerId: c.id });
      const deviceCount = await keyRepo
        .createQueryBuilder('k')
        .innerJoin('devices', 'd', 'd.key_id = k.id AND d.status = 1')
        .where('k.customer_id = :cid', { cid: c.id })
        .getCount();
      return { ...c, keyCount, deviceCount };
    })
  );
  ok(res, { total, page, pageSize, list: enriched });
});

// POST /api/admin/customers  新建
router.post('/customers', async (req, res) => {
  try {
    const { error, value } = Joi.object({
      name: Joi.string().required().max(64),
      phone: Joi.string().allow('', null).max(32).default(null),
      wechat: Joi.string().allow('', null).max(64).default(null),
      qq: Joi.string().allow('', null).max(32).default(null),
      source: Joi.string().allow('', null).max(64).default(null),
      remark: Joi.string().allow('', null).max(255).default(null),
    }).validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);
    const row = await repo().save(repo().create(value as Customer));
    await writeOperationLog(req.auth!.userId, '新建客户', row.name, null, clientIp(req));
    ok(res, row);
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// PUT /api/admin/customers/:id  编辑
router.put('/customers/:id', async (req, res) => {
  try {
    const row = await repo().findOneBy({ id: req.params.id });
    if (!row) return fail(res, '客户不存在', 1002);
    const { error, value } = Joi.object({
      name: Joi.string().required().max(64),
      phone: Joi.string().allow('', null).max(32).default(null),
      wechat: Joi.string().allow('', null).max(64).default(null),
      qq: Joi.string().allow('', null).max(32).default(null),
      source: Joi.string().allow('', null).max(64).default(null),
      remark: Joi.string().allow('', null).max(255).default(null),
      status: Joi.number().integer().min(0).max(1).default(1),
    }).validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);
    Object.assign(row, value);
    await repo().save(row);
    await writeOperationLog(req.auth!.userId, '编辑客户', row.name, null, clientIp(req));
    ok(res, row);
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// DELETE /api/admin/customers/:id  删除（仅停用，不物理删除）
router.delete('/customers/:id', async (req, res) => {
  try {
    const row = await repo().findOneBy({ id: req.params.id });
    if (!row) return fail(res, '客户不存在', 1002);
    row.status = 0;
    await repo().save(row);
    await writeOperationLog(req.auth!.userId, '停用客户', row.name, null, clientIp(req));
    ok(res, { id: row.id });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// GET /api/admin/customers/:id/keys  名下码
router.get('/customers/:id/keys', async (req, res) => {
  const keys = await AppDataSource.getRepository(LicenseKey).find({
    where: { customerId: req.params.id },
    order: { createdAt: 'DESC' },
    take: 200,
  });
  ok(res, keys);
});

export default router;
