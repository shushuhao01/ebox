import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

/** 官网下载地址（GitHub / 网盘 / 镜像站等，可多条） */
@Entity('site_download_mirrors')
export class SiteDownloadMirror {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ length: 64 })
  name!: string;

  @Column({ type: 'varchar', length: 500 })
  url!: string;

  @Column({ type: 'varchar', length: 32, default: 'other', comment: 'github/netdisk/mirror/other' })
  type!: string;

  @Column({ type: 'int', default: 0 })
  sort!: number;

  @Column({ type: 'tinyint', default: 1 })
  enabled!: number;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;
}
