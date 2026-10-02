-- Keep complete 2.0.1 station metadata; legacy display columns have shorter limits.
CREATE TABLE IF NOT EXISTS cpp201_station (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  charging_station_body MEDIUMTEXT NOT NULL,
  boot_reason VARCHAR(64) NOT NULL,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB;
