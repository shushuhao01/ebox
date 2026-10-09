import { Response } from 'express';
import { logger } from '../config/logger';

// ============================================================
// 服务端系统公告实时推送（SSE）连接池
//   后台修改 notice 后，向所有在线长连接即时广播；客户端收到即刷新公告栏。
//   心跳仍会随响应下发公告作为兜底，两条通道互不依赖：
//   SSE 断线不影响授权校验（心跳负责作废/过期/下线），心跳失败也不影响公告实时性。
// ============================================================

// 已建立的 SSE 连接（每个元素是一个尚未结束的 HTTP 响应）
const clients = new Set<Response>();

// 保活：每 25s 向所有连接写一行 SSE 注释，避免中间代理（Nginx/网关）按空闲超时断开
const KEEPALIVE_MS = 25 * 1000;
let keepaliveTimer: NodeJS.Timeout | null = null;

function ensureKeepalive(): void {
  if (keepaliveTimer) return;
  keepaliveTimer = setInterval(() => {
    for (const res of clients) {
      try {
        res.write(': ping\n\n');
      } catch {
        clients.delete(res);
      }
    }
  }, KEEPALIVE_MS);
  keepaliveTimer.unref?.();
}

function stopKeepaliveIfIdle(): void {
  if (keepaliveTimer && clients.size === 0) {
    clearInterval(keepaliveTimer);
    keepaliveTimer = null;
  }
}

// 写入一条 notice 事件；notice 为空串表示"公告已撤下"
function writeNotice(res: Response, notice: string): void {
  res.write(`data: ${JSON.stringify({ notice })}\n\n`);
}

/** 新客户端接入：登记连接并立即下发当前公告快照，避免等待下一次变更。 */
export function addClient(res: Response, initialNotice: string): void {
  clients.add(res);
  ensureKeepalive();
  writeNotice(res, initialNotice);
}

/** 客户端断开：注销连接。 */
export function removeClient(res: Response): void {
  clients.delete(res);
  stopKeepaliveIfIdle();
}

/** 广播公告变更到所有在线连接。 */
export function broadcastNotice(notice: string): void {
  if (clients.size === 0) return;
  let sent = 0;
  for (const res of clients) {
    try {
      writeNotice(res, notice);
      sent++;
    } catch {
      clients.delete(res);
    }
  }
  logger.info(`SSE 公告广播：${sent} 个在线连接已收到更新`);
}

/** 当前在线连接数（供后台/诊断查看）。 */
export function onlineCount(): number {
  return clients.size;
}
