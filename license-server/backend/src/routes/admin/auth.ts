import { Router } from 'express';
import Joi from 'joi';
import bcrypt from 'bcryptjs';
import { AppDataSource } from '../../config/database';
import { User } from '../../entities/User';
import { ok, fail, signToken, authRequired, clientIp } from '../../middleware/helpers';
import { ApiError } from '../../middleware/errorHandler';

const router = Router();

router.post('/login', async (req, res) => {
  try {
    const { error, value } = Joi.object({
      username: Joi.string().required().max(64),
      password: Joi.string().required().max(128),
    }).validate(req.body);
    if (error) return fail(res, '参数错误', 400);

    const user = await AppDataSource.getRepository(User).findOneBy({ username: value.username });
    if (!user || !bcrypt.compareSync(value.password, user.passwordHash)) {
      return fail(res, '用户名或密码错误', 1001);
    }
    if (user.status !== 1) return fail(res, '账号已停用', 1002);

    user.lastLoginAt = new Date();
    user.lastLoginIp = clientIp(req);
    await AppDataSource.getRepository(User).save(user);

    const token = signToken({ userId: user.id, username: user.username, role: user.role });
    ok(res, {
      token,
      user: { id: user.id, username: user.username, nickname: user.nickname, role: user.role },
    }, '登录成功');
  } catch (e) {
    if (e instanceof ApiError) return fail(res, e.message, e.code);
    throw e;
  }
});

router.get('/profile', authRequired, async (req, res) => {
  const user = await AppDataSource.getRepository(User).findOneBy({ id: req.auth!.userId });
  if (!user) return fail(res, '用户不存在', 401, 401);
  ok(res, {
    id: user.id,
    username: user.username,
    nickname: user.nickname,
    role: user.role,
    lastLoginAt: user.lastLoginAt,
    lastLoginIp: user.lastLoginIp,
  });
});

export default router;
