import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn, UpdateDateColumn } from 'typeorm';

/**
 * 官网文章 / 公告统一表（category 区分）
 * - category: tutorial(教程) / science(科普) / update(更新) / notice(公告)
 * - content_type: html(富文本) / md(Markdown)
 * - status: draft(草稿) / published(已发布)
 */
@Entity('site_articles')
export class SiteArticle {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ length: 160, unique: true })
  slug!: string;

  @Column({ length: 200 })
  title!: string;

  @Column({ type: 'varchar', length: 32, default: 'tutorial' })
  category!: string;

  @Column({ type: 'varchar', length: 500, nullable: true })
  cover!: string | null;

  @Column({ type: 'varchar', length: 500, nullable: true })
  summary!: string | null;

  @Column({ type: 'longtext', nullable: true })
  content!: string | null;

  @Column({ type: 'varchar', length: 8, name: 'content_type', default: 'html' })
  contentType!: string;

  @Column({ type: 'varchar', length: 16, default: 'draft' })
  status!: string;

  @Column({ type: 'tinyint', default: 0 })
  pinned!: number;

  @Column({ type: 'datetime', name: 'publish_at', nullable: true })
  publishAt!: Date | null;

  @Column({ type: 'varchar', length: 200, name: 'seo_title', nullable: true })
  seoTitle!: string | null;

  @Column({ type: 'varchar', length: 500, name: 'seo_desc', nullable: true })
  seoDesc!: string | null;

  @Column({ type: 'int', unsigned: true, default: 0 })
  views!: number;

  @Column({ type: 'bigint', name: 'created_by', nullable: true })
  createdBy!: string | null;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;

  @UpdateDateColumn({ name: 'updated_at', nullable: true })
  updatedAt!: Date | null;
}
