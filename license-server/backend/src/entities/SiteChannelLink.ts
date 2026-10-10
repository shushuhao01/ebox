import { Entity, PrimaryGeneratedColumn, Column, Index } from 'typeorm';

/** 官网渠道链接（推广短链，/c/{code} 与 ?ch={code}） */
@Entity('site_channel_link')
export class SiteChannelLink {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Index({ unique: true })
  @Column({ type: 'varchar', length: 32 })
  code!: string;

  @Column({ type: 'varchar', length: 128 })
  name!: string;

  @Column({ type: 'varchar', length: 64, default: '' })
  channel!: string;

  @Column({ type: 'varchar', length: 255, name: 'target_path', default: '/' })
  targetPath!: string;

  @Column({ type: 'varchar', length: 512, nullable: true })
  remark!: string | null;

  @Column({ type: 'tinyint', default: 1 })
  enabled!: number;

  @Column({ type: 'int', unsigned: true, name: 'click_count', default: 0 })
  clickCount!: number;

  @Column({ type: 'int', unsigned: true, name: 'unique_click_count', default: 0 })
  uniqueClickCount!: number;

  @Column({ type: 'datetime', name: 'created_at', default: () => 'CURRENT_TIMESTAMP' })
  createdAt!: Date;

  @Column({ type: 'datetime', name: 'updated_at', default: () => 'CURRENT_TIMESTAMP', onUpdate: 'CURRENT_TIMESTAMP' })
  updatedAt!: Date;
}
