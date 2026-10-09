import { Router } from 'express';
import express from 'express';
import fs from 'fs';
import path from 'path';
import crypto from 'crypto';
import Joi from 'joi';
import { AppDataSource } from '../../config/database';
import { SiteArticle } from '../../entities/SiteArticle';
import { SiteCase } from '../../entities/SiteCase';
import { SiteNav } from '../../entities/SiteNav';
import { SiteContact } from '../../entities/SiteContact';
import { SiteMedia } from '../../entities/SiteMedia';
import { SiteDownloadMirror } from '../../entities/SiteDownloadMirror';
import { SiteAccessRule } from '../../entities/SiteAccessRule';
import { ok, fail, clientIp } from '../../middleware/helpers';
import { writeOperationLog } from '../../services/LogService';
import {
  getSiteSettings,
  setSiteSettings,
  clearReleaseCache,
  clearAccessRuleCache,
  getRecentStats,
} from '../../services/SiteService';
import { getSiteAnalytics, listSiteVisits, getSiteOnlineCount } from '../../services/SiteAnalyticsService';

const router = Router();

// ==================== 媒体上传目录 ====================
const UPLOAD_ROOT = process.env.SITE_UPLOAD_DIR || path.resolve(process.cwd(), 'uploads');
const SITE_UPLOAD_DIR = path.join(UPLOAD_ROOT, 'site');
try {
  fs.mkdirSync(SITE_UPLOAD_DIR, { recursive: true });
} catch {
  // 目录创建失败时，上传接口会返回错误，不影响其他接口
}

const ALLOWED_MIME = ['image/jpeg', 'image/png', 'image/webp', 'image/gif', 'image/svg+xml'];
const EXT_MAP: Record<string, string> = {
  'image/jpeg': '.jpg',
  'image/png': '.png',
  'image/webp': '.webp',
  'image/gif': '.gif',
  'image/svg+xml': '.svg',
};

// 官网直下安装包目录与允许的扩展名
const RELEASE_UPLOAD_DIR = path.join(UPLOAD_ROOT, 'releases');
try {
  fs.mkdirSync(RELEASE_UPLOAD_DIR, { recursive: true });
} catch {
  // 目录创建失败时，上传接口会返回错误，不影响其他接口
}
const RELEASE_EXTS = ['.exe', '.zip'];
const RELEASE_UPLOAD_LIMIT = process.env.RELEASE_UPLOAD_LIMIT || '500mb';

/** 归一化更新日志为 JSON 数组字符串（兼容数组 / JSON 字符串 / 按行文本） */
function normalizeChangelog(v: unknown): string {
  if (v === undefined || v === null || v === '') return '[]';
  if (Array.isArray(v)) {
    return JSON.stringify(v.map((s) => String(s)).filter((s) => s.trim()));
  }
  const s = String(v).trim();
  if (!s) return '[]';
  try {
    const parsed = JSON.parse(s);
    if (Array.isArray(parsed)) return JSON.stringify(parsed.map((x) => String(x)));
  } catch {
    // 非 JSON，按行拆分
  }
  return JSON.stringify(s.split('\n').map((x) => x.trim()).filter(Boolean));
}

/** 基础 SVG 清洗：移除脚本与事件、外链嵌入等危险内容 */
function sanitizeSvg(svg: string): string {
  return svg
    .replace(/<script[\s\S]*?<\/script>/gi, '')
    .replace(/<foreignObject[\s\S]*?<\/foreignObject>/gi, '')
    .replace(/<(iframe|embed|object)[\s\S]*?<\/\1>/gi, '')
    .replace(/\son\w+\s*=\s*("[^"]*"|'[^']*'|[^\s>]+)/gi, '')
    .replace(/javascript:/gi, '');
}

/** 富文本基础清洗：保留排版标签，移除脚本 / 事件 / 危险协议 */
function sanitizeHtml(html: string): string {
  if (!html) return '';
  return html
    .replace(/<script[\s\S]*?<\/script>/gi, '')
    .replace(/<style[\s\S]*?<\/style>/gi, '')
    .replace(/<iframe[\s\S]*?<\/iframe>/gi, '')
    .replace(/<(iframe|embed|object|form)[^>]*>/gi, '')
    .replace(/\son\w+\s*=\s*("[^"]*"|'[^']*'|[^\s>]+)/gi, '')
    .replace(/javascript:/gi, '')
    .replace(/<img\b[^>]*\bsrc\s*=\s*("data:[^"]*"|'data:[^']*')[^>]*>/gi, '')
    .replace(/<a\s+([^>]*?)href\s*=\s*("javascript:[^"]*"|'javascript:[^']*')/gi, '<a $1href="#"');
}

function parseJsonArray(v: unknown): string | null {
  if (v === undefined || v === null || v === '') return null;
  if (typeof v === 'string') {
    try {
      JSON.parse(v);
      return v;
    } catch {
      return null;
    }
  }
  try {
    return JSON.stringify(v);
  } catch {
    return null;
  }
}

// ==================== 站点设置 ====================

router.get('/settings', async (_req, res) => {
  const settings = await getSiteSettings();
  ok(res, settings);
});

router.put('/settings', async (req, res) => {
  const body = req.body as Record<string, unknown>;
  if (!body || typeof body !== 'object') return fail(res, '参数错误', 400);
  const patch: Record<string, string> = {};
  for (const [k, v] of Object.entries(body)) {
    if (k === 'access_password') continue; // 口令走安全接口
    patch[k] = v === null || v === undefined ? '' : String(v);
  }
  await setSiteSettings(patch);
  clearReleaseCache();
  await writeOperationLog(req.auth!.userId, '官网-更新站点设置', 'site_settings', `更新 ${Object.keys(patch).length} 项`, clientIp(req));
  ok(res, await getSiteSettings());
});

// ==================== 安全策略 ====================

router.get('/security', async (_req, res) => {
  const all = await getSiteSettings();
  ok(res, {
    maintenance_mode: all.maintenance_mode || '0',
    access_password: all.access_password || '',
  });
});

router.put('/security', async (req, res) => {
  const { error, value } = Joi.object({
    maintenance_mode: Joi.string().valid('0', '1').default('0'),
    access_password: Joi.string().allow('').max(64).default(''),
  }).validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  await setSiteSettings(value as Record<string, string>);
  await writeOperationLog(req.auth!.userId, '官网-更新安全策略', 'site_security', `维护模式=${(value as { maintenance_mode: string }).maintenance_mode}`, clientIp(req));
  ok(res, value);
});

// ==================== 文章管理 ====================

const articleRepo = () => AppDataSource.getRepository(SiteArticle);

router.get('/articles', async (req, res) => {
  const page = Math.max(1, parseInt(String(req.query.page || '1'), 10) || 1);
  const pageSize = Math.min(100, Math.max(1, parseInt(String(req.query.pageSize || '20'), 10) || 20));
  const categories = String(req.query.category || '').split(',').map((s) => s.trim()).filter(Boolean);
  const status = typeof req.query.status === 'string' ? req.query.status : '';
  const qb = articleRepo().createQueryBuilder('a');
  if (categories.length === 1) qb.andWhere('a.category = :category', { category: categories[0] });
  else if (categories.length > 1) qb.andWhere('a.category IN (:...categories)', { categories });
  if (status) qb.andWhere('a.status = :status', { status });
  const total = await qb.getCount();
  const list = await qb.orderBy('a.pinned', 'DESC').addOrderBy('a.id', 'DESC').skip((page - 1) * pageSize).take(pageSize).getMany();
  ok(res, { total, page, pageSize, list });
});

router.post('/articles', async (req, res) => {
  const { error, value } = Joi.object({
    slug: Joi.string().allow('', null).max(160).default(''),
    title: Joi.string().required().max(200),
    category: Joi.string().valid('tutorial', 'science', 'update', 'notice').default('tutorial'),
    cover: Joi.string().allow('', null).max(500).default(null),
    summary: Joi.string().allow('', null).max(500).default(null),
    content: Joi.string().allow('', null).default(''),
    contentType: Joi.string().valid('html', 'md').default('html'),
    status: Joi.string().valid('draft', 'published').default('draft'),
    pinned: Joi.number().valid(0, 1).default(0),
    publishAt: Joi.string().allow('', null).default(null),
    expireAt: Joi.string().allow('', null).default(null),
    linkUrl: Joi.string().allow('', null).max(500).default(null),
    seoTitle: Joi.string().allow('', null).max(200).default(null),
    seoDesc: Joi.string().allow('', null).max(500).default(null),
  }).validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  const v = value as Record<string, unknown>;

  let slug = String(v.slug || '').trim();
  if (!slug) slug = `article-${Date.now()}`;
  const exists = await articleRepo().findOneBy({ slug });
  if (exists) return fail(res, 'slug 已存在，请更换', 400);

  const content = v.contentType === 'html' ? sanitizeHtml(String(v.content || '')) : String(v.content || '');
  const row = await articleRepo().save(
    articleRepo().create({
      slug,
      title: String(v.title),
      category: String(v.category),
      cover: (v.cover as string) || null,
      summary: (v.summary as string) || null,
      content,
      contentType: String(v.contentType),
      status: String(v.status),
      pinned: Number(v.pinned) || 0,
      publishAt: v.publishAt ? new Date(String(v.publishAt)) : (v.status === 'published' ? new Date() : null),
      expireAt: v.expireAt ? new Date(String(v.expireAt)) : null,
      linkUrl: (v.linkUrl as string) || null,
      seoTitle: (v.seoTitle as string) || null,
      seoDesc: (v.seoDesc as string) || null,
      createdBy: req.auth!.userId,
    })
  );
  await writeOperationLog(req.auth!.userId, '官网-新建文章', row.title, `slug=${row.slug}`, clientIp(req));
  ok(res, row);
});

router.get('/articles/:id', async (req, res) => {
  const row = await articleRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '文章不存在', 1002);
  ok(res, row);
});

router.put('/articles/:id', async (req, res) => {
  const row = await articleRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '文章不存在', 1002);
  const b = req.body as Record<string, unknown>;
  if (b.slug !== undefined) {
    const slug = String(b.slug || '').trim() || row.slug;
    if (slug !== row.slug) {
      const dup = await articleRepo().findOneBy({ slug });
      if (dup) return fail(res, 'slug 已存在，请更换', 400);
      row.slug = slug;
    }
  }
  if (b.title !== undefined) row.title = String(b.title);
  if (b.category !== undefined) row.category = String(b.category);
  if (b.cover !== undefined) row.cover = (b.cover as string) || null;
  if (b.summary !== undefined) row.summary = (b.summary as string) || null;
  if (b.contentType !== undefined) row.contentType = String(b.contentType);
  if (b.content !== undefined) row.content = row.contentType === 'html' ? sanitizeHtml(String(b.content)) : String(b.content);
  if (b.status !== undefined) row.status = String(b.status);
  if (b.pinned !== undefined) row.pinned = Number(b.pinned) ? 1 : 0;
  if (b.publishAt !== undefined) row.publishAt = b.publishAt ? new Date(String(b.publishAt)) : null;
  if (b.expireAt !== undefined) row.expireAt = b.expireAt ? new Date(String(b.expireAt)) : null;
  if (b.linkUrl !== undefined) row.linkUrl = (b.linkUrl as string) || null;
  if (b.seoTitle !== undefined) row.seoTitle = (b.seoTitle as string) || null;
  if (b.seoDesc !== undefined) row.seoDesc = (b.seoDesc as string) || null;
  const saved = await articleRepo().save(row);
  await writeOperationLog(req.auth!.userId, '官网-更新文章', saved.title, `slug=${saved.slug}`, clientIp(req));
  ok(res, saved);
});

router.delete('/articles/:id', async (req, res) => {
  const row = await articleRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '文章不存在', 1002);
  await articleRepo().delete({ id: row.id });
  await writeOperationLog(req.auth!.userId, '官网-删除文章', row.title, `slug=${row.slug}`, clientIp(req));
  ok(res, { id: row.id });
});

// ==================== 案例管理 ====================

const caseRepo = () => AppDataSource.getRepository(SiteCase);

router.get('/cases', async (req, res) => {
  const page = Math.max(1, parseInt(String(req.query.page || '1'), 10) || 1);
  const pageSize = Math.min(100, Math.max(1, parseInt(String(req.query.pageSize || '20'), 10) || 20));
  const total = await caseRepo().count();
  const list = await caseRepo().find({ order: { sort: 'ASC', id: 'DESC' }, skip: (page - 1) * pageSize, take: pageSize });
  ok(res, { total, page, pageSize, list });
});

router.post('/cases', async (req, res) => {
  const { error, value } = Joi.object({
    industry: Joi.string().allow('', null).max(64).default('通用'),
    title: Joi.string().required().max(200),
    summary: Joi.string().allow('', null).max(500).default(null),
    content: Joi.string().allow('', null).default(''),
    avatar: Joi.string().allow('', null).max(500).default(null),
    metrics: Joi.any(),
    sort: Joi.number().default(0),
    status: Joi.string().valid('draft', 'published').default('published'),
  }).validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  const v = value as Record<string, unknown>;
  const row = await caseRepo().save(
    caseRepo().create({
      industry: String(v.industry || '通用'),
      title: String(v.title),
      summary: (v.summary as string) || null,
      content: sanitizeHtml(String(v.content || '')),
      avatar: (v.avatar as string) || null,
      metrics: parseJsonArray(v.metrics),
      sort: Number(v.sort) || 0,
      status: String(v.status),
    })
  );
  await writeOperationLog(req.auth!.userId, '官网-新建案例', row.title, null, clientIp(req));
  ok(res, row);
});

router.put('/cases/:id', async (req, res) => {
  const row = await caseRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '案例不存在', 1002);
  const b = req.body as Record<string, unknown>;
  if (b.industry !== undefined) row.industry = String(b.industry || '通用');
  if (b.title !== undefined) row.title = String(b.title);
  if (b.summary !== undefined) row.summary = (b.summary as string) || null;
  if (b.content !== undefined) row.content = sanitizeHtml(String(b.content));
  if (b.avatar !== undefined) row.avatar = (b.avatar as string) || null;
  if (b.metrics !== undefined) row.metrics = parseJsonArray(b.metrics);
  if (b.sort !== undefined) row.sort = Number(b.sort) || 0;
  if (b.status !== undefined) row.status = String(b.status);
  const saved = await caseRepo().save(row);
  await writeOperationLog(req.auth!.userId, '官网-更新案例', saved.title, null, clientIp(req));
  ok(res, saved);
});

router.delete('/cases/:id', async (req, res) => {
  const row = await caseRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '案例不存在', 1002);
  await caseRepo().delete({ id: row.id });
  await writeOperationLog(req.auth!.userId, '官网-删除案例', row.title, null, clientIp(req));
  ok(res, { id: row.id });
});

// ==================== 导航菜单 ====================

const navRepo = () => AppDataSource.getRepository(SiteNav);

router.get('/nav', async (_req, res) => {
  const list = await navRepo().find({ order: { position: 'ASC', sort: 'ASC', id: 'ASC' } });
  ok(res, list);
});

router.post('/nav', async (req, res) => {
  const { error, value } = Joi.object({
    position: Joi.string().valid('top', 'footer').default('top'),
    label: Joi.string().required().max(64),
    url: Joi.string().required().max(500),
    target: Joi.string().valid('_self', '_blank').default('_self'),
    sort: Joi.number().default(0),
    visible: Joi.number().valid(0, 1).default(1),
  }).validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  const row = await navRepo().save(navRepo().create(value as Partial<SiteNav>));
  await writeOperationLog(req.auth!.userId, '官网-新建导航', row.label, null, clientIp(req));
  ok(res, row);
});

router.put('/nav/:id', async (req, res) => {
  const row = await navRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '导航不存在', 1002);
  const b = req.body as Record<string, unknown>;
  if (b.position !== undefined) row.position = String(b.position);
  if (b.label !== undefined) row.label = String(b.label);
  if (b.url !== undefined) row.url = String(b.url);
  if (b.target !== undefined) row.target = String(b.target);
  if (b.sort !== undefined) row.sort = Number(b.sort) || 0;
  if (b.visible !== undefined) row.visible = Number(b.visible) ? 1 : 0;
  const saved = await navRepo().save(row);
  await writeOperationLog(req.auth!.userId, '官网-更新导航', saved.label, null, clientIp(req));
  ok(res, saved);
});

router.delete('/nav/:id', async (req, res) => {
  const row = await navRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '导航不存在', 1002);
  await navRepo().delete({ id: row.id });
  await writeOperationLog(req.auth!.userId, '官网-删除导航', row.label, null, clientIp(req));
  ok(res, { id: row.id });
});

// ==================== 联系方式 ====================

const contactRepo = () => AppDataSource.getRepository(SiteContact);

router.get('/contacts', async (_req, res) => {
  const list = await contactRepo().find({ order: { sort: 'ASC', id: 'ASC' } });
  ok(res, list);
});

router.post('/contacts', async (req, res) => {
  const { error, value } = Joi.object({
    type: Joi.string().valid('wechat', 'wechat_service', 'qq', 'email', 'phone', 'other').default('other'),
    name: Joi.string().required().max(64),
    value: Joi.string().allow('', null).max(255).default(null),
    qrcode: Joi.string().allow('', null).max(500).default(null),
    sort: Joi.number().default(0),
    enabled: Joi.number().valid(0, 1).default(1),
  }).validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  const row = await contactRepo().save(contactRepo().create(value as Partial<SiteContact>));
  await writeOperationLog(req.auth!.userId, '官网-新建联系方式', row.name, null, clientIp(req));
  ok(res, row);
});

router.put('/contacts/:id', async (req, res) => {
  const row = await contactRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '联系方式不存在', 1002);
  const b = req.body as Record<string, unknown>;
  if (b.type !== undefined) row.type = String(b.type);
  if (b.name !== undefined) row.name = String(b.name);
  if (b.value !== undefined) row.value = (b.value as string) || null;
  if (b.qrcode !== undefined) row.qrcode = (b.qrcode as string) || null;
  if (b.sort !== undefined) row.sort = Number(b.sort) || 0;
  if (b.enabled !== undefined) row.enabled = Number(b.enabled) ? 1 : 0;
  const saved = await contactRepo().save(row);
  await writeOperationLog(req.auth!.userId, '官网-更新联系方式', saved.name, null, clientIp(req));
  ok(res, saved);
});

router.delete('/contacts/:id', async (req, res) => {
  const row = await contactRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '联系方式不存在', 1002);
  await contactRepo().delete({ id: row.id });
  await writeOperationLog(req.auth!.userId, '官网-删除联系方式', row.name, null, clientIp(req));
  ok(res, { id: row.id });
});

// ==================== 下载地址（镜像 / 网盘） ====================

const mirrorRepo = () => AppDataSource.getRepository(SiteDownloadMirror);

router.get('/mirrors', async (_req, res) => {
  const list = await mirrorRepo().find({ order: { sort: 'ASC', id: 'ASC' } });
  ok(res, list);
});

router.post('/mirrors', async (req, res) => {
  const { error, value } = Joi.object({
    name: Joi.string().required().max(64),
    url: Joi.string().required().max(500),
    type: Joi.string().valid('github', 'netdisk', 'mirror', 'other').default('other'),
    password: Joi.string().allow('', null).max(128).default(null),
    extractCode: Joi.string().allow('', null).max(64).default(null),
    sort: Joi.number().default(0),
    enabled: Joi.number().valid(0, 1).default(1),
  }).validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  const row = await mirrorRepo().save(mirrorRepo().create(value as Partial<SiteDownloadMirror>));
  clearReleaseCache();
  await writeOperationLog(req.auth!.userId, '官网-新增下载地址', row.name, row.url, clientIp(req));
  ok(res, row);
});

router.put('/mirrors/:id', async (req, res) => {
  const row = await mirrorRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '下载地址不存在', 1002);
  const b = req.body as Record<string, unknown>;
  if (b.name !== undefined) row.name = String(b.name);
  if (b.url !== undefined) row.url = String(b.url);
  if (b.type !== undefined) row.type = String(b.type);
  if (b.password !== undefined) row.password = (b.password as string) || null;
  if (b.extractCode !== undefined) row.extractCode = (b.extractCode as string) || null;
  if (b.sort !== undefined) row.sort = Number(b.sort) || 0;
  if (b.enabled !== undefined) row.enabled = Number(b.enabled) ? 1 : 0;
  const saved = await mirrorRepo().save(row);
  clearReleaseCache();
  await writeOperationLog(req.auth!.userId, '官网-更新下载地址', saved.name, saved.url, clientIp(req));
  ok(res, saved);
});

router.delete('/mirrors/:id', async (req, res) => {
  const row = await mirrorRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '下载地址不存在', 1002);
  await mirrorRepo().delete({ id: row.id });
  clearReleaseCache();
  await writeOperationLog(req.auth!.userId, '官网-删除下载地址', row.name, row.url, clientIp(req));
  ok(res, { id: row.id });
});

// ==================== 访问控制规则 ====================

const ruleRepo = () => AppDataSource.getRepository(SiteAccessRule);

router.get('/access-rules', async (_req, res) => {
  const list = await ruleRepo().find({ order: { type: 'ASC', id: 'ASC' } });
  ok(res, list);
});

router.post('/access-rules', async (req, res) => {
  const { error, value } = Joi.object({
    type: Joi.string().valid('ip_black', 'ip_white', 'ua').required(),
    pattern: Joi.string().required().max(255),
    note: Joi.string().allow('', null).max(255).default(null),
    enabled: Joi.number().valid(0, 1).default(1),
  }).validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  const row = await ruleRepo().save(ruleRepo().create(value as Partial<SiteAccessRule>));
  clearAccessRuleCache();
  await writeOperationLog(req.auth!.userId, '官网-新增访问规则', row.type, row.pattern, clientIp(req));
  ok(res, row);
});

router.put('/access-rules/:id', async (req, res) => {
  const row = await ruleRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '规则不存在', 1002);
  const b = req.body as Record<string, unknown>;
  if (b.type !== undefined) row.type = String(b.type);
  if (b.pattern !== undefined) row.pattern = String(b.pattern);
  if (b.note !== undefined) row.note = (b.note as string) || null;
  if (b.enabled !== undefined) row.enabled = Number(b.enabled) ? 1 : 0;
  const saved = await ruleRepo().save(row);
  clearAccessRuleCache();
  await writeOperationLog(req.auth!.userId, '官网-更新访问规则', saved.type, saved.pattern, clientIp(req));
  ok(res, saved);
});

router.delete('/access-rules/:id', async (req, res) => {
  const row = await ruleRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '规则不存在', 1002);
  await ruleRepo().delete({ id: row.id });
  clearAccessRuleCache();
  await writeOperationLog(req.auth!.userId, '官网-删除访问规则', row.type, row.pattern, clientIp(req));
  ok(res, { id: row.id });
});

// ==================== 媒体库 ====================

const mediaRepo = () => AppDataSource.getRepository(SiteMedia);

router.get('/media', async (req, res) => {
  const page = Math.max(1, parseInt(String(req.query.page || '1'), 10) || 1);
  const pageSize = Math.min(100, Math.max(1, parseInt(String(req.query.pageSize || '24'), 10) || 24));
  const total = await mediaRepo().count();
  const list = await mediaRepo().find({ order: { id: 'DESC' }, skip: (page - 1) * pageSize, take: pageSize });
  ok(res, { total, page, pageSize, list });
});

/**
 * 图片上传：接收原始二进制（Content-Type 为图片类型），文件名通过 query.filename 传入。
 * 返回可直接用于 <img> / 富文本的路径（/api/uploads/site/xxx）。
 */
router.post('/media/upload', express.raw({ type: () => true, limit: '10mb' }), async (req, res) => {
  try {
    const mime = String(req.headers['content-type'] || '').split(';')[0].trim();
    if (!ALLOWED_MIME.includes(mime)) {
      return fail(res, '仅支持 jpg / png / webp / gif / svg 图片', 400);
    }
    const buf = req.body as Buffer;
    if (!Buffer.isBuffer(buf) || !buf.length) return fail(res, '上传内容为空', 400);
    if (buf.length > 10 * 1024 * 1024) return fail(res, '图片大小不能超过 10MB', 400);

    let data: Buffer = buf;
    if (mime === 'image/svg+xml') {
      data = Buffer.from(sanitizeSvg(buf.toString('utf8')), 'utf8');
    }

    const orig = String(req.query.filename || 'image');
    const base = path.basename(orig).replace(/\.[^.]*$/, '').replace(/[^a-zA-Z0-9_-]/g, '').slice(0, 40) || 'image';
    const ext = EXT_MAP[mime] || '.png';
    const filename = `${Date.now()}-${Math.random().toString(36).slice(2, 8)}-${base}${ext}`;
    fs.writeFileSync(path.join(SITE_UPLOAD_DIR, filename), data);

    const url = `/api/uploads/site/${filename}`;
    const row = await mediaRepo().save(
      mediaRepo().create({
        filename,
        path: url,
        size: data.length,
        mime,
        width: null,
        height: null,
        uploadedBy: req.auth!.userId,
      })
    );
    await writeOperationLog(req.auth!.userId, '官网-上传图片', filename, null, clientIp(req));
    ok(res, { id: row.id, url, filename });
  } catch (e) {
    return fail(res, e instanceof Error ? e.message : '上传失败', 500, 500);
  }
});

router.delete('/media/:id', async (req, res) => {
  const row = await mediaRepo().findOneBy({ id: req.params.id });
  if (!row) return fail(res, '媒体不存在', 1002);
  try {
    const file = path.join(SITE_UPLOAD_DIR, path.basename(row.path));
    if (fs.existsSync(file)) fs.unlinkSync(file);
  } catch {
    // 文件删除失败不影响记录删除
  }
  await mediaRepo().delete({ id: row.id });
  await writeOperationLog(req.auth!.userId, '官网-删除图片', row.filename, null, clientIp(req));
  ok(res, { id: row.id });
});

// ==================== 官网直下（版本与安装包） ====================

/** 从全部设置中挑选官网直下相关字段 */
function pickReleaseConfig(all: Record<string, string>) {
  return {
    release_version: all.release_version || '',
    release_date: all.release_date || '',
    release_changelog: all.release_changelog || '[]',
    release_file_url: all.release_file_url || '',
    release_file_name: all.release_file_name || '',
    release_file_size: all.release_file_size || '0',
    release_file_sha256: all.release_file_sha256 || '',
  };
}

router.get('/release', async (_req, res) => {
  ok(res, pickReleaseConfig(await getSiteSettings()));
});

router.put('/release', async (req, res) => {
  const { error, value } = Joi.object({
    release_version: Joi.string().allow('', null).max(64).default(''),
    release_date: Joi.string().allow('', null).max(32).default(''),
    release_changelog: Joi.any(),
  }).validate(req.body);
  if (error) return fail(res, `参数错误：${error.message}`, 400);
  const v = value as Record<string, unknown>;
  await setSiteSettings({
    release_version: String(v.release_version || ''),
    release_date: String(v.release_date || ''),
    release_changelog: normalizeChangelog(v.release_changelog),
  });
  clearReleaseCache();
  await writeOperationLog(
    req.auth!.userId,
    '官网-更新版本信息',
    String(v.release_version || ''),
    `日期=${v.release_date || '-'}`,
    clientIp(req)
  );
  ok(res, pickReleaseConfig(await getSiteSettings()));
});

/**
 * 上传安装包：接收原始二进制（Content-Type 不限），文件名通过 query.filename 传入。
 * 仅允许 .exe / .zip，成功后写入 release_* 设置并返回最新配置；旧安装包文件自动清理。
 */
router.post('/release/upload', express.raw({ type: () => true, limit: RELEASE_UPLOAD_LIMIT }), async (req, res) => {
  try {
    const buf = req.body as Buffer;
    if (!Buffer.isBuffer(buf) || !buf.length) return fail(res, '上传内容为空', 400);

    const orig = String(req.query.filename || '');
    const ext = path.extname(orig).toLowerCase();
    if (!RELEASE_EXTS.includes(ext)) return fail(res, '仅支持 .exe / .zip 安装包', 400);

    const base =
      path.basename(orig, path.extname(orig)).replace(/[^a-zA-Z0-9_-]/g, '').slice(0, 60) || 'release';
    const filename = `${Date.now()}-${Math.random().toString(36).slice(2, 8)}-${base}${ext}`;
    fs.writeFileSync(path.join(RELEASE_UPLOAD_DIR, filename), buf);

    const url = `/api/uploads/releases/${filename}`;
    const sha256 = crypto.createHash('sha256').update(buf).digest('hex');

    // 清理上一个安装包文件（仅限本站 releases 目录）
    const all = await getSiteSettings();
    const oldUrl = all.release_file_url || '';
    if (oldUrl.startsWith('/api/uploads/releases/')) {
      try {
        const oldFile = path.join(RELEASE_UPLOAD_DIR, path.basename(oldUrl));
        if (fs.existsSync(oldFile)) fs.unlinkSync(oldFile);
      } catch {
        // 旧文件删除失败不影响新包上传
      }
    }

    await setSiteSettings({
      release_file_url: url,
      release_file_name: orig ? path.basename(orig) : filename,
      release_file_size: String(buf.length),
      release_file_sha256: sha256,
    });
    clearReleaseCache();
    await writeOperationLog(
      req.auth!.userId,
      '官网-上传安装包',
      orig || filename,
      `${(buf.length / 1048576).toFixed(1)}MB`,
      clientIp(req)
    );
    ok(res, pickReleaseConfig(await getSiteSettings()));
  } catch (e) {
    return fail(res, e instanceof Error ? e.message : '上传失败', 500, 500);
  }
});

// ==================== 版本同步 / 统计 ====================

router.post('/release/sync', async (req, res) => {
  clearReleaseCache();
  await writeOperationLog(req.auth!.userId, '官网-同步版本信息', 'latest-release', null, clientIp(req));
  ok(res, { synced: true });
});

router.get('/stats', async (req, res) => {
  const days = Math.min(90, Math.max(1, parseInt(String(req.query.days || '30'), 10) || 30));
  const list = await getRecentStats(days);
  const totals = list.reduce(
    (acc, r) => {
      acc.pv += r.pv;
      acc.uv += r.uv;
      acc.downloads += r.downloads;
      acc.buyClicks += r.buyClicks;
      return acc;
    },
    { pv: 0, uv: 0, downloads: 0, buyClicks: 0 }
  );
  ok(res, { list, totals });
});

// 官网数据分析（流量 / 来源 / 地域 / 设备 / 时间多维聚合）
router.get('/analytics', async (req, res) => {
  const days = Math.min(90, Math.max(1, parseInt(String(req.query.days || '30'), 10) || 30));
  const data = await getSiteAnalytics(days);
  ok(res, data);
});

// 官网实时在线访客数（最近 N 分钟，默认 5 分钟）
router.get('/online', async (req, res) => {
  const minutes = Math.min(1440, Math.max(1, parseInt(String(req.query.minutes || '5'), 10) || 5));
  const online = await getSiteOnlineCount(minutes);
  ok(res, { online, minutes });
});

// 官网访问明细分页查询
router.get('/visits', async (req, res) => {
  const str = (v: unknown) => (typeof v === 'string' ? v.trim() : '');
  const data = await listSiteVisits({
    page: parseInt(String(req.query.page || '1'), 10) || 1,
    pageSize: parseInt(String(req.query.pageSize || '20'), 10) || 20,
    ip: str(req.query.ip),
    path: str(req.query.path),
    device: str(req.query.device),
    source: str(req.query.source),
    keyword: str(req.query.keyword),
    start: str(req.query.start),
    end: str(req.query.end),
  });
  ok(res, data);
});

export default router;
