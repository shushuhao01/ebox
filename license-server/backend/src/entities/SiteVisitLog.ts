import { Entity, PrimaryGeneratedColumn, Column, Index } from 'typeorm';

/** 官网访问明细（逐条记录，用于流量 / 来源 / 地域 / 设备分析） */
@Entity('site_visit_log')
export class SiteVisitLog {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Index()
  @Column({ type: 'date', name: 'visit_date' })
  visitDate!: string;

  @Column({ type: 'tinyint', unsigned: true, name: 'visit_hour', default: 0 })
  visitHour!: number;

  @Index()
  @Column({ type: 'varchar', length: 64, default: '' })
  ip!: string;

  @Column({ type: 'varchar', length: 16, default: '' })
  country!: string;

  @Column({ type: 'varchar', length: 32, default: '' })
  province!: string;

  @Column({ type: 'varchar', length: 32, default: '' })
  city!: string;

  @Column({ type: 'varchar', length: 64, default: '' })
  isp!: string;

  @Column({ type: 'varchar', length: 16, default: 'unknown' })
  device!: string;

  @Column({ type: 'varchar', length: 32, default: '' })
  os!: string;

  @Column({ type: 'varchar', length: 32, default: '' })
  browser!: string;

  @Column({ type: 'varchar', length: 24, default: '其他' })
  source!: string;

  @Column({ type: 'varchar', length: 512, default: '' })
  referer!: string;

  @Index()
  @Column({ type: 'varchar', length: 255, default: '/' })
  path!: string;

  @Index()
  @Column({ type: 'varchar', length: 64, name: 'visitor_id', default: '' })
  visitorId!: string;

  @Column({ type: 'varchar', length: 255, name: 'user_agent', default: '' })
  userAgent!: string;

  @Index()
  @Column({ type: 'datetime', name: 'created_at', default: () => 'CURRENT_TIMESTAMP' })
  createdAt!: Date;
}
