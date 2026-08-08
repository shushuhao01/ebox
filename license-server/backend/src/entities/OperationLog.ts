import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

@Entity('operation_logs')
export class OperationLog {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'bigint', name: 'user_id' })
  userId!: string;

  @Column({ length: 64, comment: '生成/作废/恢复/踢下线/设置...' })
  action!: string;

  @Column({ type: 'varchar', length: 160, nullable: true })
  target!: string | null;

  @Column({ type: 'varchar', length: 255, nullable: true })
  detail!: string | null;

  @Column({ type: 'varchar', length: 64, nullable: true })
  ip!: string | null;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;
}
