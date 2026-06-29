export interface Hive {
  id: number;
  name: string;
  location: string | null;
  latitude: number | null;
  longitude: number | null;
  active: boolean;
  created_at: Date;
}

export interface Reading {
  id: number;
  hive_id: number;
  weight: number | null;
  temperature: number | null;
  humidity: number | null;
  battery_mv: number | null;
  rssi: number | null;
  recorded_at: Date;
}

export interface HiveWithLatest extends Hive {
  latest: Reading | null;
}

export interface User {
  id: number;
  email: string;
  password: string;
  created_at: Date;
}
