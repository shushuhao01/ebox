import dayjs from 'dayjs'
import { ElMessage } from 'element-plus'

/** 格式化时间，null/空返回 '-' */
export function formatTime(v: string | number | Date | null | undefined, pattern = 'YYYY-MM-DD HH:mm:ss'): string {
  if (v === null || v === undefined || v === '') return '-'
  const d = dayjs(v)
  return d.isValid() ? d.format(pattern) : String(v)
}

/** 格式化日期 */
export function formatDate(v: string | number | Date | null | undefined): string {
  return formatTime(v, 'YYYY-MM-DD')
}

/** 时长秒数 -> 人类可读（0=永久） */
export function formatDuration(sec: string | number | null | undefined): string {
  const s = Number(sec ?? 0)
  if (!s) return '永久'
  const days = Math.floor(s / 86400)
  if (days >= 365 && days % 365 === 0) return `${days / 365}年`
  return `${days}天`
}

/** Unix 秒时间戳 -> 日期（换机码到期时间） */
export function formatUnixTs(v: string | number | null | undefined): string {
  if (v === null || v === undefined || v === '') return '-'
  const n = Number(v)
  if (!Number.isFinite(n) || n <= 0) return '-'
  const d = dayjs.unix(n)
  return d.isValid() ? d.format('YYYY-MM-DD HH:mm') : String(v)
}

/** 复制文本到剪贴板 */
export async function copyText(text: string, tip = '已复制') {
  try {
    await navigator.clipboard.writeText(text)
    ElMessage.success(tip)
  } catch {
    // 降级方案：临时 textarea
    const ta = document.createElement('textarea')
    ta.value = text
    ta.style.position = 'fixed'
    ta.style.opacity = '0'
    document.body.appendChild(ta)
    ta.select()
    try {
      document.execCommand('copy')
      ElMessage.success(tip)
    } catch {
      ElMessage.error('复制失败')
    }
    document.body.removeChild(ta)
  }
}

/** 激活码状态映射 */
export const KEY_STATUS: Record<number, { label: string; type: 'info' | 'success' | 'danger' | 'warning' }> = {
  0: { label: '未用', type: 'info' },
  1: { label: '已用', type: 'success' },
  2: { label: '作废', type: 'danger' },
  3: { label: '过期', type: 'warning' },
  4: { label: '已换机', type: 'warning' },
  5: { label: '已删除', type: 'info' },
}

/** 激活码类型映射 */
export const KEY_TYPE: Record<number, { label: string; type: 'info' | 'success' }> = {
  1: { label: '库存', type: 'info' },
  2: { label: '换机', type: 'success' },
}

/** 心跳动作映射 */
export const HB_ACTION: Record<number, { label: string; type: 'info' | 'success' | 'warning' | 'danger' }> = {
  1: { label: '激活', type: 'success' },
  2: { label: '心跳', type: 'info' },
  3: { label: '解绑', type: 'warning' },
  4: { label: '被踢', type: 'danger' },
}

/** 规范化 IP 展示：兼容旧数据 IPv4 映射 IPv6（::ffff:1.2.3.4 → 1.2.3.4）、IPv6 回环（::1 → 127.0.0.1） */
export function formatIp(v: string | null | undefined): string {
  if (!v) return '-'
  const mapped = v.match(/^::ffff:(\d+\.\d+\.\d+\.\d+)$/i)
  if (mapped) return mapped[1]
  if (v === '::1') return '127.0.0.1'
  return v
}

/** 心跳流水详情中文映射（兼容旧数据英文，新数据后端直接存中文） */
const HB_DETAIL_MAP: Record<string, string> = {
  activate: '客户端在线激活成功',
  heartbeat: '客户端心跳上报，在线状态正常',
  'revoked heartbeat': '心跳检测到激活码已被作废，本机授权已锁定',
  'expired heartbeat': '心跳检测到激活码已过期，本机授权已锁定',
  'kicked heartbeat': '心跳检测到激活码被其他设备强制下线，本机授权已锁定',
}

/** 心跳流水详情展示：英文旧数据 → 中文详细描述；无法识别时原样返回 */
export function formatHbDetail(detail: string | null): string {
  if (!detail) return '-'
  if (detail.startsWith('unbind ->')) {
    return `解绑换机，服务端已签发新激活码：${detail.slice('unbind ->'.length).trim()}`
  }
  return HB_DETAIL_MAP[detail] || detail
}

/** 解绑上限展示 */
export function formatUnbindMax(v: number): string {
  if (v === -1) return '不限'
  if (v === 0) return '禁止'
  return `每月${v}次`
}

/** 在线判断：lastOnlineAt 距今 <= 阈值(默认30分钟) */
export function isOnline(lastOnlineAt: string | null | undefined, thresholdMin = 30): boolean {
  if (!lastOnlineAt) return false
  const t = dayjs(lastOnlineAt)
  if (!t.isValid()) return false
  return Date.now() - t.valueOf() <= thresholdMin * 60 * 1000
}

/** 状态灯 class */
export function deviceStatusClass(status: number, online: boolean): string {
  if (status === 2) return 'kicked'
  if (status === 0) return 'unbound'
  return online ? 'online' : 'offline'
}

/** 设备状态文案 */
export function deviceStatusText(status: number, online: boolean): string {
  if (status === 2) return '被踢'
  if (status === 0) return '已解绑'
  return online ? '在线' : '离线'
}
