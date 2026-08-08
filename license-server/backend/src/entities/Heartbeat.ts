import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

@Entity('heartbeats')
export class Heartbeat {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'bigint', name: 'key_id' })
  keyId!: string;

  @Column({ type: 'bigint', name: 'device_id', nullable: true })
  deviceId!: string | null;

  @Column({ type: 'tinyint', comment: '1=激活 2=心跳 3=解绑 4=被踢' })
  action!: number;

  @Column({ type: 'bigint', name: 'client_time' })
  clientTime!: string;

  @Column({ type: 'varchar', length: 64, nullable: true })
  ip!: string | null;

  @Column({ type: 'varchar', length: 32, name: 'app_version', nullable: true })
  appVersion!: string | null;

  @Column({ type: 'varchar', length: 255, nullable: true })
  detail!: string | null;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;
}
