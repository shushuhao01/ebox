import { Router } from 'express';
import { addClient, removeClient } from '../services/NoticeBroadcaster';
import { getConfigValue } from '../services/ConfigService';
import { findByCode } from '../services/KeyService';
import { AppDataSource } from '../config/database';
import { Device } from '../entities/Device';

const router = Router();

// GET /api/v1/notice/stream?code=&machineFp=
//   服务端 → 客户端 系统公告实时推送（SSE 长连接）。
//   鉴权：code 已登记且该 machineFp 为在绑设备（仅已激活客户端可连）。
//   事件格式：data: {"notice":"..."}   空串表示公告已撤下。
//   连上即下发当前公告快照，之后仅在变更时推送；另每 25s 发送 ": ping" 保活注释。
router.get('/notice/stream', async (req, res) => {
  const code = String(req.query.code || '');
  const fp = String(req.query.machineFp || '');

  const key = code ? await findByCode(code) : null;
  if (!key) {
    res.status(403).json({ code: 403, msg: '未授权', data: null });
    return;
  }
  const device = await AppDataSource.getRepository(Device).findOneBy({ keyId: key.id, machineFp: fp });
  if (!device || device.status !== 1) {
    res.status(403).json({ code: 403, msg: '未授权', data: null });
    return;
  }

  // 建立 SSE 长连接
  res.status(200);
  res.setHeader('Content-Type', 'text/event-stream; charset=utf-8');
  res.setHeader('Cache-Control', 'no-cache, no-transform');
  res.setHeader('Connection', 'keep-alive');
  // 关闭 Nginx 缓冲（宝塔反代默认开启），保证事件即时下发，无需改动 Nginx 配置
  res.setHeader('X-Accel-Buffering', 'no');
  res.flushHeaders?.();

  addClient(res, await getConfigValue('notice'));

  const cleanup = () => removeClient(res);
  req.on('close', cleanup);
  req.on('error', cleanup);
});

export default router;
