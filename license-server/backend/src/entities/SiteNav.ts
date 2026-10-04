import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

/** 官网导航菜单（顶部 / 底部） */
@Entity('site_nav')
export class SiteNav {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'varchar', length: 16, default: 'top', comment: 'top=顶部 footer=底部' })
  position!: string;

  @Column({ length: 64 })
  label!: string;

  @Column({ type: 'varchar', length: 500 })
  url!: string;

  @Column({ type: 'varchar', length: 16, default: '_self', comment: '_self / _blank' })
  target!: string;

  @Column({ type: 'int', default: 0 })
  sort!: number;

  @Column({ type: 'tinyint', default: 1 })
  visible!: number;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;
}
