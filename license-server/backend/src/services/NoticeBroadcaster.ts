import { Response } from 'express';
import { logger } from '../config/logger';

// ============================================================
// 服务端系统公告实时推送（SSE）连接池
//   后台保存/发送公告后，向所有在线长连接即时广播；客户端收到即刷新公告栏。
//   心跳仍会随响应下发公告作为兜底，两条通道互不依赖：
//   SSE 断线不影响授权校验（心跳负责作废/过期/下线），心跳失败也不影响公告实时性。
// ============================================================

/** 结构化公告（客户端用于横幅两行标题 + 详情弹窗 + 链接跳转） */
export interface NoticeData {
  title: string;
  content: string;
  strong: boolean;
  link: string;
}

/** SSE / 心跳统一下发格式：notice 为老客户端兼容串（标题或正文首行），noticeData 为结构化数据 */
export interface NoticeEvent {
  notice: string;
  noticeData: NoticeData | null;
}

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

// 写入一条公告事件；notice 为空串且 noticeData 为 null 表示"公告已撤下"
function writeNotice(res: Response, event: NoticeEvent): void {
  res.write(`data: ${JSON.stringify(event)}\n\n`);
}

/** 新客户端接入：登记连接并立即下发当前公告快照，避免等待下一次变更。 */
export function addClient(res: Response, initial: NoticeEvent): void {
  clients.add(res);
  ensureKeepalive();
  writeNotice(res, initial);
}

/** 客户端断开：注销连接。 */
export function removeClient(res: Response): void {
  clients.delete(res);
  stopKeepaliveIfIdle();
}

/** 广播公告变更到所有在线连接，返回实际写入成功的连接数。 */
export function broadcastNotice(event: NoticeEvent): number {
  const total = clients.size;
  if (total === 0) {
    logger.info('SSE 公告广播：当前无客户端保持实时连接，公告将在客户端下次心跳/启动时同步');
    return 0;
  }
  let sent = 0;
  for (const res of clients) {
    try {
      writeNotice(res, event);
      sent++;
    } catch {
      clients.delete(res);
    }
  }
  logger.info(`SSE 公告广播：${sent}/${total} 个在线连接已收到更新`);
  return sent;
}

/** 当前在线连接数（供后台/诊断查看）。 */
export function onlineCount(): number {
  return clients.size;
}
