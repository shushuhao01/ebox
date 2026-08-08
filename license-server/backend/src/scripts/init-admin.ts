// 初始化管理员账号
// 用法：npm run init:admin -- --username admin --password xxxx [--nickname 管理员] [--role 1]
import { loadEnv } from './loadEnv';
loadEnv();

import bcrypt from 'bcryptjs';
import 'reflect-metadata';
import { AppDataSource } from '../config/database';
import { User } from '../entities/User';

function arg(name: string): string {
  const idx = process.argv.indexOf(`--${name}`);
  return idx >= 0 && process.argv[idx + 1] ? process.argv[idx + 1] : '';
}

async function main() {
  const username = arg('username') || 'admin';
  let password = arg('password') || '';
  if (!password) {
    password = 'admin123';
    console.log('⚠️  未指定 --password，使用默认密码 admin123（请尽快修改！）');
  }
  if (password.length < 6) {
    console.error('密码长度至少 6 位');
    process.exit(1);
  }
  await AppDataSource.initialize();
  const repo = AppDataSource.getRepository(User);
  const exists = await repo.findOneBy({ username });
  if (exists) {
    exists.passwordHash = bcrypt.hashSync(password, 10);
    exists.role = Number(arg('role') || exists.role);
    await repo.save(exists);
    console.log(`✅ 管理员 ${username} 已更新密码`);
  } else {
    await repo.save(
      repo.create({
        username,
        passwordHash: bcrypt.hashSync(password, 10),
        nickname: arg('nickname') || '管理员',
        role: Number(arg('role') || 1),
        status: 1,
      })
    );
    console.log(`✅ 管理员 ${username} 创建成功`);
  }
  await AppDataSource.destroy();
}

main().catch((e) => {
  console.error('初始化失败：', e);
  process.exit(1);
});
