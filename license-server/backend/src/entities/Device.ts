import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

@Entity('devices')
export class Device {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'bigint', name: 'key_id' })
  keyId!: string;

  @Column({ length: 16, name: 'machine_fp' })
  machineFp!: string;

  @Column({ type: 'tinyint', default: 1, comment: '1=在绑 0=已解绑 2=被踢' })
  status!: number;

  @Column({ name: 'first_activate_at', type: 'datetime', nullable: true })
  firstActivateAt!: Date | null;

  @Column({ name: 'last_online_at', type: 'datetime', nullable: true })
  lastOnlineAt!: Date | null;

  @Column({ name: 'last_heartbeat_at', type: 'datetime', nullable: true })
  lastHeartbeatAt!: Date | null;

  @Column({ type: 'varchar', length: 64, name: 'last_ip', nullable: true })
  lastIp!: string | null;

  @Column({ type: 'varchar', length: 128, name: 'os_info', nullable: true })
  osInfo!: string | null;

  @Column({ type: 'varchar', length: 32, name: 'app_version', nullable: true })
  appVersion!: string | null;

  @Column({ name: 'unbind_at', type: 'datetime', nullable: true })
  unbindAt!: Date | null;
}
