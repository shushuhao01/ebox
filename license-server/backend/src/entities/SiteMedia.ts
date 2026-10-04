import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

/** 官网媒体库 */
@Entity('site_media')
export class SiteMedia {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ length: 255 })
  filename!: string;

  @Column({ type: 'varchar', length: 500 })
  path!: string;

  @Column({ type: 'int', unsigned: true, default: 0 })
  size!: number;

  @Column({ type: 'varchar', length: 64, nullable: true })
  mime!: string | null;

  @Column({ type: 'int', unsigned: true, nullable: true })
  width!: number | null;

  @Column({ type: 'int', unsigned: true, nullable: true })
  height!: number | null;

  @Column({ type: 'bigint', name: 'uploaded_by', nullable: true })
  uploadedBy!: string | null;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;
}
