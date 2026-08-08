import { Router } from 'express';
import Joi from 'joi';
import bcrypt from 'bcryptjs';
import { AppDataSource } from '../../config/database';
import { User } from '../../entities/User';
import { ok, fail, authRequired, superAdminRequired, clientIp } from '../../middleware/helpers';
import { ApiError } from '../../middleware/errorHandler';
import { writeOperationLog } from '../../services/LogService';

const router = Router();

router.get('/', authRequired, superAdminRequired, async (_req, res) => {
  const rows = await AppDataSource.getRepository(User).find({ order: { createdAt: 'DESC' } });
  ok(res, rows.map((u) => ({ id: u.id, username: u.username, nickname: u.nickname, role: u.role, status: u.status, lastLoginAt: u.lastLoginAt, createdAt: u.createdAt })));
});

router.post('/', authRequired, superAdminRequired, async (req, res) => {
  try {
    const { error, value } = Joi.object({
      username: Joi.string().required().max(64),
      password: Joi.string().required().min(6).max(128),
      nickname: Joi.string().allow('', null).max(64).default(null),
      role: Joi.number().integer().min(0).max(1).default(0),
    }).validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);
    const repo = AppDataSource.getRepository(User);
    if (await repo.findOneBy({ username: value.username })) return fail(res, '用户名已存在', 1003);
    const row = await repo.save(repo.create({ ...(value as User), passwordHash: bcrypt.hashSync(value.password, 10) }));
    await writeOperationLog(req.auth!.userId, '新建管理员', row.username, null, clientIp(req));
    ok(res, { id: row.id, username: row.username });
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// 修改密码（本人或超管）
router.put('/password', authRequired, async (req, res) => {
  try {
    const { error, value } = Joi.object({
      oldPassword: Joi.string().required().max(128),
      newPassword: Joi.string().required().min(6).max(128),
    }).validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);
    const repo = AppDataSource.getRepository(User);
    const user = await repo.findOneBy({ id: req.auth!.userId });
    if (!user || !bcrypt.compareSync(value.oldPassword, user.passwordHash)) return fail(res, '原密码错误', 1004);
    user.passwordHash = bcrypt.hashSync(value.newPassword, 10);
    await repo.save(user);
    await writeOperationLog(req.auth!.userId, '修改密码', user.username, null, clientIp(req));
    ok(res, null, '修改成功');
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

// 停用/启用管理员（仅超管；不允许操作自己）
router.put('/:id/status', authRequired, superAdminRequired, async (req, res) => {
  try {
    const { error, value } = Joi.object({
      status: Joi.number().integer().valid(0, 1).required(),
    }).validate(req.body);
    if (error) return fail(res, `参数错误：${error.message}`, 400);
    const repo = AppDataSource.getRepository(User);
    const id = String(req.params.id || '');
    if (!id || !/^\d+$/.test(id)) return fail(res, '参数错误', 400);
    if (id === String(req.auth!.userId)) return fail(res, '不能停用自己', 400);
    const user = await repo.findOneBy({ id });
    if (!user) return fail(res, '管理员不存在', 1003);
    user.status = value.status;
    await repo.save(user);
    await writeOperationLog(req.auth!.userId, value.status === 1 ? '启用管理员' : '停用管理员', user.username, null, clientIp(req));
    ok(res, null, '操作成功');
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

export default router;
