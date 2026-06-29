CREATE TABLE IF NOT EXISTS hives (
  id         INT AUTO_INCREMENT PRIMARY KEY,
  name       VARCHAR(100) NOT NULL,
  location   VARCHAR(200),
  latitude   DECIMAL(10, 8),
  longitude  DECIMAL(11, 8),
  active     BOOLEAN DEFAULT TRUE,
  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS readings (
  id          INT AUTO_INCREMENT PRIMARY KEY,
  hive_id     INT NOT NULL,
  weight      DECIMAL(6, 2),
  temperature DECIMAL(4, 1),
  humidity    DECIMAL(4, 1),
  battery_mv  INT,
  rssi        INT,
  recorded_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (hive_id) REFERENCES hives(id)
);

CREATE INDEX IF NOT EXISTS idx_readings_hive_date ON readings(hive_id, recorded_at DESC);

-- test hive so you can POST a reading immediately
INSERT INTO hives (name, location) VALUES ('Vcela 1', 'Zahrada');
