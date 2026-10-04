import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

/** 官网联系方式 */
@Entity('site_contacts')
export class SiteContact {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'varchar', length: 32, default: 'other', comment: 'wechat/qq/email/phone/other' })
  type!: string;

  @Column({ length: 64 })
  name!: string;

  @Column({ type: 'varchar', length: 255, nullable: true })
  value!: string | null;

  @Column({ type: 'varchar', length: 500, nullable: true, comment: '二维码图片地址' })
  qrcode!: string | null;

  @Column({ type: 'int', default: 0 })
  sort!: number;

  @Column({ type: 'tinyint', default: 1 })
  enabled!: number;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;
}
