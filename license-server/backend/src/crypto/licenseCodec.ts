import crypto from 'crypto';
import { env } from '../config/env';

// ============================================================
// eBox 激活码编解码（与 eBox 客户端 C++ biz::license 完全兼容）
//   激活码 = 载荷(18B) + ECDSA P-256 签名(64B raw r||s)，共 82 字节
//   编码：RFC4648 Base32（无 padding）+ 文本 "EBOX-" + 每 5 字符 "-"（旧码前缀 2BOX- 仍兼容）
//   载荷布局：
//     [0..1] magic "BO"
//     [2]    verMajor = 2
//     [3]    verMinor（8=离线时长制(KeyGen发码,未托管)；9=在线时长制(面板发码,服务端托管)；
//            <=7 旧格式绝对到期）
//     [4..7] LE：时长制=有效时长秒(0=永久)；旧格式=绝对到期时间戳
//     [8]    指纹标志 1=绑定 0=通用
//     [9..16] 机器指纹 8B（生成时置 0，激活时客户端自动绑定本机）
//     [17]   每自然月最大解绑次数（0=禁止 0xFF=不限 n=次数）
// ============================================================

const MAGIC = [0x42, 0x4f]; // "BO"
const VERSION_MAJOR = 2;
const VERSION_MINOR_DURATION = 8; // 离线时长制（与客户端 KeyGen 同款，未托管）
const VERSION_MINOR_ONLINE = 9; // 在线时长制（面板生成，服务端登记托管）
const VERSION_MINOR_LEGACY = 7; // 换机码（绝对到期）

// 与客户端一致的 P-256 公钥（X/Y，公开信息，来自 eBox kPublicKeyBlob）
const PUB_X = Buffer.from('2a8913c461d144a18659bba92fb7962d3807f243a7d2fc3952cb1d1a34adc5a5', 'hex');
const PUB_Y = Buffer.from('8164be694723aeeaea4df54fdf6eff09794cce7136875844d112ed23411612f6', 'hex');

const BASE32_ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';

function base32Encode(data: Buffer): string {
  let out = '';
  let buffer = 0;
  let bits = 0;
  for (const b of data) {
    buffer = (buffer << 8) | b;
    bits += 8;
    while (bits >= 5) {
      out += BASE32_ALPHABET[(buffer >> (bits - 5)) & 0x1f];
      bits -= 5;
    }
  }
  if (bits > 0) out += BASE32_ALPHABET[(buffer << (5 - bits)) & 0x1f];
  return out;
}

function base32Decode(text: string): Buffer {
  const out: number[] = [];
  let buffer = 0;
  let bits = 0;
  for (const c of text) {
    if (c === '=') continue;
    let value = -1;
    if (c >= 'A' && c <= 'Z') value = c.charCodeAt(0) - 65;
    else if (c >= 'a' && c <= 'z') value = c.charCodeAt(0) - 97;
    else if (c >= '2' && c <= '7') value = c.charCodeAt(0) - 50 + 26;
    if (value < 0) continue;
    buffer = (buffer << 5) | value;
    bits += 5;
    if (bits >= 8) {
      out.push((buffer >> (bits - 8)) & 0xff);
      bits -= 8;
    }
  }
  return Buffer.from(out);
}

// 文本规范化：去空格/'-'/'2BOX'/'EBOX'前缀，转大写（与客户端 parseAndVerifyCode 一致）
export function normalizeCode(code: string): string {
  let upper = '';
  for (const c of code) {
    if (c === ' ' || c === '-') continue;
    if (c >= 'a' && c <= 'z') upper += c.toUpperCase();
    else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) upper += c;
  }
  // 兼容旧前缀 2BOX 与新前缀 EBOX（4 字符）
  if (upper.startsWith('2BOX') || upper.startsWith('EBOX')) upper = upper.slice(4);
  return upper;
}

export interface ParsedLicense {
  valid: boolean;
  isDurationFormat: boolean;
  onlineManaged: boolean; // verMinor==9 在线托管码（面板生成、服务端登记）
  durationSec: number; // 时长制有效秒数，0=永久
  expire: number; // 旧格式绝对到期时间戳
  bound: boolean;
  unbindMax: number; // -1=不限
}

// 解码并验签（服务端无登记信息时仍可确认"签名有效"）
export function decodeAndVerify(code: string): ParsedLicense | null {
  const normalized = normalizeCode(code);
  if (normalized.length < 20) return null;
  const raw = base32Decode(normalized);
  if (raw.length !== 82) return null;
  const payload = raw.subarray(0, 18);
  const signature = raw.subarray(18, 82);

  // magic + 版本
  if (payload[0] !== MAGIC[0] || payload[1] !== MAGIC[1]) return null;
  if (payload[2] !== VERSION_MAJOR || payload[3] > VERSION_MINOR_ONLINE) return null;

  // 验签（SHA-256 + raw r||s，与 BCryptVerifySignature 一致）
  const hash = crypto.createHash('sha256').update(payload).digest();
  const publicKey = crypto.createPublicKey({
    key: {
      kty: 'EC',
      crv: 'P-256',
      x: PUB_X.toString('base64url'),
      y: PUB_Y.toString('base64url'),
    },
    format: 'jwk',
  });
  let ok = false;
  try {
    ok = crypto.verify(null, hash, { key: publicKey, dsaEncoding: 'ieee-p1363' }, signature);
  } catch {
    ok = false;
  }
  if (!ok) return null;

  let field = 0;
  for (let i = 0; i < 4; i++) field |= payload[4 + i] << (i * 8);
  const isDurationFormat = payload[3] === VERSION_MINOR_DURATION || payload[3] === VERSION_MINOR_ONLINE;
  const onlineManaged = payload[3] === VERSION_MINOR_ONLINE;
  const bound = payload[8] !== 0;
  let unbindMax = payload[17];
  if (unbindMax === 0xff) unbindMax = -1;

  return {
    valid: true,
    isDurationFormat,
    onlineManaged,
    durationSec: isDurationFormat ? field : 0,
    expire: isDurationFormat ? 0 : field,
    bound,
    unbindMax,
  };
}

// 私钥对象（懒加载）：用 env 中的 d 标量 + 内置公钥 X/Y 构造
let privateKeyCache: crypto.KeyObject | null = null;

function getPrivateKey(): crypto.KeyObject | null {
  if (!env.privateKeyD) return null;
  if (!privateKeyCache) {
    try {
      privateKeyCache = crypto.createPrivateKey({
        key: {
          kty: 'EC',
          crv: 'P-256',
          d: Buffer.from(env.privateKeyD, 'hex').toString('base64url'),
          x: PUB_X.toString('base64url'),
          y: PUB_Y.toString('base64url'),
        },
        format: 'jwk',
      });
    } catch {
      return null;
    }
  }
  return privateKeyCache;
}

// 签名 18 字节载荷，返回 64 字节 raw r||s（与 BCryptSignHash 输出一致）
function signPayload(payload: Buffer): Buffer | null {
  const key = getPrivateKey();
  if (!key) return null;
  const hash = crypto.createHash('sha256').update(payload).digest();
  try {
    return crypto.sign(null, hash, { key, dsaEncoding: 'ieee-p1363' });
  } catch {
    return null;
  }
}

// 生成激活码。
//   durationFormat=true：
//     onlineManaged=true（默认，面板库存码）：field=有效时长秒（0=永久），格式 verMinor=9（在线托管）
//     onlineManaged=false（离线码/与客户端 KeyGen 同款）：field=有效时长秒，格式 verMinor=8（未托管）
//   durationFormat=false（换机码）：field=绝对到期时间戳（秒），格式 verMinor=7
export function buildLicenseCode(
  field: number,
  bound: boolean,
  unbindMaxPerMonth: number,
  durationFormat = true,
  onlineManaged = true
): string | null {
  const payload = Buffer.alloc(18, 0);
  payload[0] = MAGIC[0];
  payload[1] = MAGIC[1];
  payload[2] = VERSION_MAJOR;
  payload[3] = durationFormat
    ? (onlineManaged ? VERSION_MINOR_ONLINE : VERSION_MINOR_DURATION)
    : VERSION_MINOR_LEGACY;
  const f = BigInt(field);
  for (let i = 0; i < 4; i++) payload[4 + i] = Number((f >> BigInt(i * 8)) & 0xffn);
  payload[8] = bound ? 1 : 0;
  // [9..16] 指纹留 0；非绑定码 [17]=0
  if (bound) {
    if (unbindMaxPerMonth < 0) payload[17] = 0xff;
    else payload[17] = Math.min(Math.max(unbindMaxPerMonth, 0), 255);
  }
  const signature = signPayload(payload);
  if (!signature || signature.length !== 64) return null;

  const raw = Buffer.concat([payload, signature]);
  const b32 = base32Encode(raw);
  let text = 'EBOX';
  for (let i = 0; i < b32.length; i++) {
    if (i % 5 === 0) text += '-';
    text += b32[i];
  }
  return text;
}

// 码的 SHA-256 指纹（hex，用于快速查询/防重放）
export function codeFingerprint(code: string): string {
  return crypto.createHash('sha256').update(normalizeCode(code)).digest('hex');
}
