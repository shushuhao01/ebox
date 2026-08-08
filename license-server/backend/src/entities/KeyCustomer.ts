import { Entity, PrimaryGeneratedColumn, Column, CreateDateColumn } from 'typeorm';

@Entity('key_customer')
export class KeyCustomer {
  @PrimaryGeneratedColumn({ type: 'bigint', unsigned: true })
  id!: string;

  @Column({ type: 'bigint', name: 'key_id' })
  keyId!: string;

  @Column({ type: 'bigint', name: 'customer_id' })
  customerId!: string;

  @CreateDateColumn({ name: 'created_at' })
  createdAt!: Date;
}
