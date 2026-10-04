import { Entity, PrimaryGeneratedColumn, Column } from 'typeorm';

/** 官网自身访问统计（按天聚合） */
@Entity('site_stats_daily')
export class SiteStatsDaily {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'date', name: 'stat_date', unique: true })
  statDate!: string;

  @Column({ type: 'int', unsigned: true, default: 0 })
  pv!: number;

  @Column({ type: 'int', unsigned: true, default: 0 })
  uv!: number;

  @Column({ type: 'int', unsigned: true, default: 0 })
  downloads!: number;

  @Column({ type: 'int', unsigned: true, name: 'buy_clicks', default: 0 })
  buyClicks!: number;
}
