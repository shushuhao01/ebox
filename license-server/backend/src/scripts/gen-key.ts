// 生成/查看 ECDSA 密钥信息
// 用法：npm run gen:key
// 由 .env 中 LICENSE_PRIVATE_KEY_D 派生公钥坐标，并与客户端内置的可信公钥逐一比对
import crypto from 'crypto';
import { loadEnv } from './loadEnv';

loadEnv();

// eBox 客户端内置公钥（公开信息，双公钥过渡期同时信任旧、新两把）
const TRUSTED_KEYS = [
  {
    label: '旧公钥',
    x: '2a8913c461d144a18659bba92fb7962d3807f243a7d2fc3952cb1d1a34adc5a5',
    y: '8164be694723aeeaea4df54fdf6eff09794cce7136875844d112ed23411612f6',
  },
  {
    label: '新公钥',
    x: '91572c6ea68429c82912babdfa26a80f3bd448278ca748a417280b918dc05028',
    y: 'bd0a1e8090d7a8e71dadde551676fca142c8d8a244c9d1102ae8563536af0c3c',
  },
];

const d = process.env.LICENSE_PRIVATE_KEY_D || '';

console.log('==============================================');
console.log('eBox 授权服务平台 - ECDSA 密钥信息');
console.log('==============================================');
if (!d) {
  console.log('⚠️  未配置 LICENSE_PRIVATE_KEY_D');
  console.log('   请在 .env 中配置与 eBox 客户端配套的私钥标量 d（hex）');
  console.log('   私钥需与 eBox 客户端内置公钥配套，仅离线保管，禁止写入仓库、脚本或文档');
  process.exit(0);
}
try {
  // 由私钥标量 d 派生公钥（不能反过来用内置公钥拼私钥，那样比对恒为真）
  const ecdh = crypto.createECDH('prime256v1');
  ecdh.setPrivateKey(Buffer.from(d, 'hex'));
  const point = ecdh.getPublicKey(); // 0x04 || X(32) || Y(32)
  const x = point.subarray(1, 33).toString('hex');
  const y = point.subarray(33, 65).toString('hex');

  const hit = TRUSTED_KEYS.find((k) => k.x === x && k.y === y);

  console.log(`公钥 X     : ${x}`);
  console.log(`公钥 Y     : ${y}`);
  console.log('');
  console.log(
    hit
      ? `与客户端内置公钥比对：✅ 命中「${hit.label}」（可正常签名发码）`
      : '与客户端内置公钥比对：❌ 未命中任一内置公钥（生成的码客户端将无法验签！）'
  );
} catch (e) {
  console.error('私钥解析失败，请检查 LICENSE_PRIVATE_KEY_D 是否为 64 位 hex：', (e as Error).message);
}
