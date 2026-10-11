import { Router } from 'express';
import { buildSitemapXml, getSeoConfig } from '../services/SeoService';
import { log } from '../config/logger';

/**
 * 根级 SEO 路由（不挂在 /api 下，直接暴露在站点根路径）：
 *   GET /sitemap.xml         动态 sitemap（固定页面 + 全部可见文章）
 *   GET /<indexnow-key>.txt  IndexNow 密钥校验文件（Bing 等据此验证归属）
 * 由 Nginx 将这两个路径反代到后端（详见 deploy/nginx/abc222.cn.conf）。
 */
const router = Router();

router.get('/sitemap.xml', async (_req, res) => {
  try {
    const xml = await buildSitemapXml();
    res.set('Content-Type', 'application/xml; charset=utf-8');
    res.set('Cache-Control', 'public, max-age=600');
    res.send(xml);
  } catch (e) {
    log.warn('[SEO] 生成 sitemap 失败', String(e));
    res.status(500).type('text/plain').send('sitemap 生成失败');
  }
});

// IndexNow 密钥文件：仅匹配十六进制命名的 .txt，避免误伤 robots.txt 等静态文件
router.get(/^\/([0-9a-fA-F]{8,128})\.txt$/, async (req, res) => {
  try {
    const key = String((req.params as Record<string, string>)[0] || '');
    const cfg = await getSeoConfig();
    if (cfg.indexnowKey && key === cfg.indexnowKey) {
      res.set('Content-Type', 'text/plain; charset=utf-8');
      res.set('Cache-Control', 'public, max-age=3600');
      return res.send(cfg.indexnowKey);
    }
  } catch (e) {
    log.warn('[SEO] 返回 IndexNow key 失败', String(e));
  }
  res.status(404).type('text/plain').send('Not Found');
});

export default router;
