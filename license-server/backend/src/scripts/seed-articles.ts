// 官网文章初始化脚本：把 6 篇 SEO 长尾词文章写入 site_articles，使其出现在后台「文章管理」
// 用法：npm run seed:articles
// 特性：幂等——成功后在 system_config 写入版本标记，重复执行会跳过，
//       不会覆盖后台人工编辑，也不会复活人工删除的文章。
import fs from 'fs';
import path from 'path';
import { loadEnv } from './loadEnv';
loadEnv();

import 'reflect-metadata';
import { AppDataSource } from '../config/database';
import { SiteArticle } from '../entities/SiteArticle';
import { SystemConfig } from '../entities/SystemConfig';
import { SEED_ARTICLES } from './seed-data/articles';

// 种子版本标记：修改 SEED_VERSION 才会重新执行（可用于后续追加新文章）
const MARKER_KEY = 'site_articles_seed_version';
const SEED_VERSION = '1';

// ==================== Markdown -> HTML ====================
// 仅覆盖文章用到的语法：标题 / 粗体 / 斜体 / 行内代码 / 无序列表 / 有序列表 /
// 引用块 / 分隔线 / 表格 / 段落。正文入库为 HTML（contentType=html），前端 v-html 直出。

function escapeHtml(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function inline(text: string): string {
  let t = escapeHtml(text.trim());
  t = t.replace(/`([^`]+)`/g, '<code>$1</code>');
  t = t.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
  t = t.replace(/(^|[^*])\*([^*]+)\*/g, '$1<em>$2</em>');
  return t;
}

const RE_HR = /^\s*---+\s*$/;
const RE_HEADING = /^(#{1,6})\s+(.*)$/;
const RE_TABLE_ROW = /^\s*\|.*\|\s*$/;
const RE_TABLE_SEP = /^\s*\|[\s:|-]+\|\s*$/;
const RE_QUOTE = /^\s*>\s?/;
const RE_UL = /^\s*[-*]\s+/;
const RE_OL = /^\s*\d+\.\s+/;

function isBlockStart(line: string): boolean {
  return (
    RE_HR.test(line) ||
    RE_HEADING.test(line) ||
    RE_TABLE_ROW.test(line) ||
    /^\s*>/.test(line) ||
    RE_UL.test(line) ||
    RE_OL.test(line)
  );
}

function splitRow(line: string): string[] {
  return line
    .trim()
    .replace(/^\|/, '')
    .replace(/\|$/, '')
    .split('|')
    .map((c) => c.trim());
}

function mdToHtml(md: string): string {
  const lines = md.replace(/\r\n?/g, '\n').split('\n');
  const out: string[] = [];
  let i = 0;

  while (i < lines.length) {
    const line = lines[i];

    if (!line.trim()) {
      i += 1;
      continue;
    }

    if (RE_HR.test(line)) {
      out.push('<hr>');
      i += 1;
      continue;
    }

    const heading = line.match(RE_HEADING);
    if (heading) {
      const level = heading[1].length;
      out.push(`<h${level}>${inline(heading[2])}</h${level}>`);
      i += 1;
      continue;
    }

    // 表格：当前行为表头，下一行为分隔行
    if (RE_TABLE_ROW.test(line) && i + 1 < lines.length && RE_TABLE_SEP.test(lines[i + 1])) {
      const header = splitRow(line);
      i += 2;
      const rows: string[][] = [];
      while (i < lines.length && RE_TABLE_ROW.test(lines[i])) {
        rows.push(splitRow(lines[i]));
        i += 1;
      }
      const thead = `<thead><tr>${header.map((c) => `<th>${inline(c)}</th>`).join('')}</tr></thead>`;
      const tbody = `<tbody>${rows
        .map((r) => `<tr>${r.map((c) => `<td>${inline(c)}</td>`).join('')}</tr>`)
        .join('')}</tbody>`;
      out.push(`<table>${thead}${tbody}</table>`);
      continue;
    }

    // 引用块：连续的 > 行
    if (/^\s*>/.test(line)) {
      const buf: string[] = [];
      while (i < lines.length && /^\s*>/.test(lines[i])) {
        buf.push(lines[i].replace(RE_QUOTE, ''));
        i += 1;
      }
      const inner = buf.map((b) => (b.trim() ? `<p>${inline(b)}</p>` : '')).join('');
      out.push(`<blockquote>${inner}</blockquote>`);
      continue;
    }

    // 无序列表
    if (RE_UL.test(line)) {
      const items: string[] = [];
      while (i < lines.length && RE_UL.test(lines[i])) {
        items.push(`<li>${inline(lines[i].replace(RE_UL, ''))}</li>`);
        i += 1;
      }
      out.push(`<ul>${items.join('')}</ul>`);
      continue;
    }

    // 有序列表
    if (RE_OL.test(line)) {
      const items: string[] = [];
      while (i < lines.length && RE_OL.test(lines[i])) {
        items.push(`<li>${inline(lines[i].replace(RE_OL, ''))}</li>`);
        i += 1;
      }
      out.push(`<ol>${items.join('')}</ol>`);
      continue;
    }

    // 普通段落：合并连续非空且非块级起始的行
    const para: string[] = [line.trim()];
    i += 1;
    while (i < lines.length && lines[i].trim() && !isBlockStart(lines[i])) {
      para.push(lines[i].trim());
      i += 1;
    }
    out.push(`<p>${inline(para.join(' '))}</p>`);
  }

  return out.join('\n');
}

// 与后台文章接口一致的富文本清洗（保留排版标签，移除脚本 / 事件 / 危险协议）
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

// ==================== 入库 ====================

async function main(): Promise<void> {
  await AppDataSource.initialize();
  const cfgRepo = AppDataSource.getRepository(SystemConfig);
  const articleRepo = AppDataSource.getRepository(SiteArticle);

  const marker = await cfgRepo.findOneBy({ cfgKey: MARKER_KEY });
  if (marker && marker.cfgValue === SEED_VERSION) {
    console.log(`ℹ️  官网文章已初始化（${MARKER_KEY}=${SEED_VERSION}），跳过`);
    await AppDataSource.destroy();
    return;
  }

  let created = 0;
  let skipped = 0;

  for (const item of SEED_ARTICLES) {
    const exists = await articleRepo.findOneBy({ slug: item.slug });
    if (exists) {
      skipped += 1;
      continue;
    }

    const filePath = path.join(__dirname, 'seed-data', item.file);
    if (!fs.existsSync(filePath)) {
      throw new Error(`正文文件不存在：${filePath}`);
    }
    const content = sanitizeHtml(mdToHtml(fs.readFileSync(filePath, 'utf8')));

    await articleRepo.save(
      articleRepo.create({
        slug: item.slug,
        title: item.title,
        category: item.category,
        cover: null,
        summary: item.summary,
        content,
        contentType: 'html',
        status: 'published',
        pinned: item.pinned ? 1 : 0,
        publishAt: new Date(),
        seoTitle: item.seoTitle,
        seoDesc: item.seoDesc,
        createdBy: null,
      })
    );
    created += 1;
    console.log(`✅ 新增文章：${item.title}（/articles/${item.slug}）`);
  }

  // 写入版本标记，保证仅初始化一次
  const remark = `官网文章种子版本（${new Date().toISOString()}）`;
  if (marker) {
    marker.cfgValue = SEED_VERSION;
    marker.remark = remark;
    await cfgRepo.save(marker);
  } else {
    await cfgRepo.save(cfgRepo.create({ cfgKey: MARKER_KEY, cfgValue: SEED_VERSION, remark }));
  }

  console.log(`🎉 官网文章初始化完成：新增 ${created} 篇，跳过已存在 ${skipped} 篇`);
  await AppDataSource.destroy();
}

main().catch((e) => {
  console.error('官网文章初始化失败：', (e as Error).message);
  console.log('   1) 确认 MySQL 已启动、.env 中 DB_* 配置正确');
  console.log('   2) 确认数据库表已初始化（npm run init:db）');
  process.exit(1);
});
