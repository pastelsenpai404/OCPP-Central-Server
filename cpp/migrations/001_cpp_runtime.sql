-- Additive migration. Apply explicitly to a BACKUP/isolated clone of the legacy schema first.
-- Never run Flyway's historical migrations against an already populated database.
CREATE TABLE IF NOT EXISTS cpp_ocpp_replay (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  message_id VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  request_body MEDIUMTEXT NOT NULL,
  response_body MEDIUMTEXT NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(station_id,message_id),
  INDEX replay_retention(created_at)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp_event_outbox (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  station_id VARCHAR(64) NOT NULL,
  action VARCHAR(64) NOT NULL,
  payload MEDIUMTEXT NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  delivered_at TIMESTAMP(6) NULL,
  INDEX outbox_delivery(delivered_at,id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp_audit (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  action VARCHAR(64) NOT NULL,
  station_id VARCHAR(64) NOT NULL,
  command_id VARCHAR(36) NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  INDEX audit_time(created_at)
) ENGINE=InnoDB;
