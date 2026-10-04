import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

/**
 * 官网访问控制规则
 * - type: ip_black(IP黑名单) / ip_white(IP白名单) / ua(UA拦截)
 * - pattern: IP / CIDR / UA 关键字或正则
 */
@Entity('site_access_rules')
export class SiteAccessRule {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'varchar', length: 16 })
  type!: string;

  @Column({ type: 'varchar', length: 255 })
  pattern!: string;

  @Column({ type: 'varchar', length: 255, nullable: true })
  note!: string | null;

  @Column({ type: 'tinyint', default: 1 })
  enabled!: number;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;
}
