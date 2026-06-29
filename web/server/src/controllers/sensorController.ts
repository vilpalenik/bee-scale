import { Request, Response } from 'express';
import { saveReading } from '../models/reading';

export async function postReading(req: Request, res: Response): Promise<void> {
  const { hive_id, weight, temperature, humidity, battery_mv, rssi } = req.body;

  if (!hive_id) {
    res.status(400).json({ success: false, message: 'hive_id is required' });
    return;
  }

  const id = await saveReading(hive_id, weight ?? null, temperature ?? null, humidity ?? null, battery_mv ?? null, rssi ?? null);
  res.status(201).json({ success: true, data: { id } });
}
