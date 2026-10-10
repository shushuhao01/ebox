import { Entity, PrimaryGeneratedColumn, Column, Index } from 'typeorm';

/** 官网渠道日聚合统计（按 渠道 + 日期 记录下载 / 购买点击） */
@Entity('site_channel_stats_daily')
@Index('uk_scsd_date_channel', ['statDate', 'channelCode'], { unique: true })
export class SiteChannelStatsDaily {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'date', name: 'stat_date' })
  statDate!: string;

  @Column({ type: 'varchar', length: 32, name: 'channel_code', default: '' })
  channelCode!: string;

  @Column({ type: 'int', unsigned: true, default: 0 })
  downloads!: number;

  @Column({ type: 'int', unsigned: true, name: 'buy_clicks', default: 0 })
  buyClicks!: number;
}
