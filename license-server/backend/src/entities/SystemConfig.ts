import { Entity, PrimaryGeneratedColumn, Column, UpdateDateColumn } from 'typeorm';

@Entity('system_config')
export class SystemConfig {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ length: 64, name: 'cfg_key', unique: true })
  cfgKey!: string;

  @Column({ length: 255, name: 'cfg_value' })
  cfgValue!: string;

  @Column({ type: 'varchar', length: 255, nullable: true })
  remark!: string | null;

  @UpdateDateColumn({ name: 'updated_at', nullable: true })
  updatedAt!: Date | null;
}
