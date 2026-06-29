import pool from '../config/db';
import { Reading } from '../types';

export async function saveReading(
  hive_id: number,
  weight: number | null,
  temperature: number | null,
  humidity: number | null,
  battery_mv: number | null,
  rssi: number | null,
): Promise<number> {
  const [result] = await pool.query(
    `INSERT INTO readings (hive_id, weight, temperature, humidity, battery_mv, rssi)
     VALUES (?, ?, ?, ?, ?, ?)`,
    [hive_id, weight, temperature, humidity, battery_mv, rssi],
  );
  return (result as any).insertId;
}
