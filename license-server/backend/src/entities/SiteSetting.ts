import { Entity, PrimaryGeneratedColumn, Column, UpdateDateColumn } from 'typeorm';

/** 官网站点设置（KV，值可为 JSON 字符串），与授权平台 system_config 物理隔离 */
@Entity('site_settings')
export class SiteSetting {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ length: 64, name: 'cfg_key', unique: true })
  cfgKey!: string;

  @Column({ type: 'text', name: 'cfg_value', nullable: true })
  cfgValue!: string | null;

  @Column({ type: 'varchar', length: 32, name: 'cfg_group', default: 'general' })
  cfgGroup!: string;

  @UpdateDateColumn({ name: 'updated_at', nullable: true })
  updatedAt!: Date | null;
}
