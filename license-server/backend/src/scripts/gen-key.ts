// 生成/查看 ECDSA 密钥信息
// 用法：npm run gen:key
// 输出当前 .env 中 LICENSE_PRIVATE_KEY_D 对应的公钥坐标，并与客户端内置公钥比对
import crypto from 'crypto';
import { loadEnv } from './loadEnv';

loadEnv();

// eBox 客户端内置公钥（公开信息）
const PUB_X = '2a8913c461d144a18659bba92fb7962d3807f243a7d2fc3952cb1d1a34adc5a5';
const PUB_Y = '8164be694723aeeaea4df54fdf6eff09794cce7136875844d112ed23411612f6';

const d = process.env.LICENSE_PRIVATE_KEY_D || '';

console.log('==============================================');
console.log('eBox 授权服务平台 - ECDSA 密钥信息');
console.log('==============================================');
if (!d) {
  console.log('⚠️  未配置 LICENSE_PRIVATE_KEY_D');
  console.log('   请在 .env 中配置与 eBox 客户端配套的私钥标量 d（hex）');
  console.log('   出厂默认（与当前 eBox 客户端内置公钥配套）：');
  console.log('   1e34ab0973d3c60a02c1fdae954c4d4d9492084fafb6428a17a86991a277d66e');
  process.exit(0);
}
try {
  const key = crypto.createPrivateKey({
    key: {
      kty: 'EC',
      crv: 'P-256',
      d: Buffer.from(d, 'hex').toString('base64url'),
      x: Buffer.from(PUB_X, 'hex').toString('base64url'),
      y: Buffer.from(PUB_Y, 'hex').toString('base64url'),
    },
    format: 'jwk',
  });
  const jwk = key.export({ format: 'jwk' });
  console.log(`私钥标量 d : ${d}`);
  console.log(`公钥 X     : ${Buffer.from(jwk.x!, 'base64url').toString('hex')}`);
  console.log(`公钥 Y     : ${Buffer.from(jwk.y!, 'base64url').toString('hex')}`);
  console.log('');
  console.log(`与客户端公钥比对：${jwk.x === Buffer.from(PUB_X, 'hex').toString('base64url') && jwk.y === Buffer.from(PUB_Y, 'hex').toString('base64url') ? '✅ 匹配（可正常签名发码）' : '❌ 不匹配（生成的码客户端将无法验签！）'}`);
} catch (e) {
  console.error('私钥解析失败，请检查 LICENSE_PRIVATE_KEY_D 是否为 64 位 hex：', (e as Error).message);
}
