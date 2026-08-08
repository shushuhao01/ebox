// 加载环境变量（脚本共用）：生产读 .env，开发优先 .env.local
import dotenv from 'dotenv';
import path from 'path';
import fs from 'fs';

export function loadEnv(): void {
  const isProduction = process.env.NODE_ENV === 'production';
  let envFile = '.env';
  if (!isProduction) {
    const local = path.join(__dirname, '../../.env.local');
    if (fs.existsSync(local)) envFile = '.env.local';
  }
  dotenv.config({ path: path.join(__dirname, '../../', envFile) });
}
