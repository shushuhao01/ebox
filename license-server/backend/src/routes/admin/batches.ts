import { Router } from 'express';
import Joi from 'joi';
import { AppDataSource } from '../../config/database';
import { KeyBatch } from '../../entities/KeyBatch';
import { LicenseKey } from '../../entities/LicenseKey';
import { ok, fail, clientIp } from '../../middleware/helpers';
import { ApiError } from '../../middleware/errorHandler';
import { writeOperationLog } from '../../services/LogService';

const router = Router();
const repo = () => AppDataSource.getRepository(KeyBatch);

router.get('/batches', async (req, res) => {
  const page = Math.max(1, parseInt(String(req.query.page || '1'), 10) || 1);
  const pageSize = Math.min(100, Math.max(1, parseInt(String(req.query.pageSize || '20'), 10) || 20));
  const total = await repo().count();
  const rows = await repo().find({ order: { createdAt: 'DESC' }, skip: (page - 1) * pageSize, take: pageSize });
  const keyRepo = AppDataSource.getRepository(LicenseKey);
  const enriched = await Promise.all(
    rows.map(async (b) => {
      const totalCount = await AppDataSource.getRepository(LicenseKey)
        .createQueryBuilder('k')
        .where('k.batch_id = :bid', { bid: b.id })
        .andWhere('k.status != 5') // 不含已删除
        .getCount();
      const usedCount = await keyRepo.countBy({ batchId: b.id, status: 1 });
      return { ...b, totalCount, usedCount };
    })
  );
  ok(res, { total, page, pageSize, list: enriched });
});

router.post('/batches', async (req, res) => {
  const { error, value } = Joi.object({
    name: Joi.string().required().max(64),
    remark: Joi.string().allow('', null).max(255).default(null),
  }).validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  const row = await repo().save(repo().create({ ...(value as KeyBatch), createdBy: req.auth!.userId }));
  await writeOperationLog(req.auth!.userId, '新建批次', row.name, null, clientIp(req));
  ok(res, row);
});

router.get('/batches/:id', async (req, res) => {
  const batch = await repo().findOneBy({ id: req.params.id });
  if (!batch) return fail(res, '批次不存在', 1002);
  const keys = await AppDataSource.getRepository(LicenseKey)
    .createQueryBuilder('k')
    .where('k.batch_id = :bid', { bid: batch.id })
    .andWhere('k.status != 5') // 不含已删除
    .orderBy('k.created_at', 'DESC')
    .take(500)
    .getMany();
  ok(res, { batch, keys });
});

// DELETE /api/admin/batches/:id  删除批次（仅删除批次本身，激活码保留并解除归属）
router.delete('/batches/:id', async (req, res) => {
  try {
    const batch = await repo().findOneBy({ id: req.params.id });
    if (!batch) return fail(res, '批次不存在', 1002);
    // 批次下激活码的 batch_id 置空（保留激活码，仅解除批次归属）
    await AppDataSource.getRepository(LicenseKey)
      .createQueryBuilder()
      .update(LicenseKey)
      .set({ batchId: null })
      .where('batch_id = :bid', { bid: batch.id })
      .execute();
    await repo().delete({ id: batch.id });
    await writeOperationLog(req.auth!.userId, '删除批次', batch.name, `批次#${batch.id} 已删除，其下激活码已解除归属（码本身保留）`, clientIp(req));
    ok(res, { id: batch.id });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

export default router;
