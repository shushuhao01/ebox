import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

@Entity('unbind_logs')
export class UnbindLog {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'bigint', name: 'key_id' })
  keyId!: string;

  @Column({ type: 'bigint', name: 'device_id' })
  deviceId!: string;

  @Column({ length: 6, comment: 'yyyyMM' })
  month!: string;

  @Column({ type: 'bigint', name: 'new_key_id', nullable: true })
  newKeyId!: string | null;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;
}
