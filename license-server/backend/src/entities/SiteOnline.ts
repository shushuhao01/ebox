import { Entity, PrimaryColumn, Column, Index } from 'typeorm';

/** 官网在线访客（实时在线统计：按访客标识记录最近一次活跃时间） */
@Entity('site_online')
export class SiteOnline {
  @PrimaryColumn({ type: 'varchar', length: 64, name: 'visitor_id' })
  visitorId!: string;

  @Column({ type: 'varchar', length: 64, default: '' })
  ip!: string;

  @Index()
  @Column({ type: 'datetime', name: 'last_seen', default: () => 'CURRENT_TIMESTAMP' })
  lastSeen!: Date;
}
