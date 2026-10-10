import { Entity, PrimaryGeneratedColumn, Column, Index } from 'typeorm';

/** 官网渠道点击明细（短链 / 带参访问时逐条记录） */
@Entity('site_channel_click')
export class SiteChannelClick {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Index()
  @Column({ type: 'varchar', length: 32, name: 'channel_code', default: '' })
  channelCode!: string;

  @Column({ type: 'varchar', length: 64, default: '' })
  ip!: string;

  @Column({ type: 'varchar', length: 255, name: 'user_agent', default: '' })
  userAgent!: string;

  @Column({ type: 'varchar', length: 512, default: '' })
  referer!: string;

  @Index()
  @Column({ type: 'datetime', name: 'created_at', default: () => 'CURRENT_TIMESTAMP' })
  createdAt!: Date;
}
