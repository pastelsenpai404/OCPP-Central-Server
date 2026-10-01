-- Explicit additive migration for the complete command lifecycle.
CREATE TABLE IF NOT EXISTS cpp_command_task (
  command_id VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  action VARCHAR(64) NOT NULL,
  protocol VARCHAR(16) NOT NULL DEFAULT 'ocpp1.6',
  request_body MEDIUMTEXT NOT NULL,
  response_body MEDIUMTEXT NULL,
  state VARCHAR(32) NOT NULL,
  http_status INT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  completed_at TIMESTAMP(6) NULL,
  INDEX task_station(station_id,created_at),
  INDEX task_state(state,created_at)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp_station_state (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
  local_list_version INT NOT NULL DEFAULT -1,
  diagnostics_status VARCHAR(64) NULL,
  firmware_status VARCHAR(64) NULL,
  log_status VARCHAR(64) NULL,
  log_request_id INT NULL,
  firmware_request_id INT NULL,
  configuration_body MEDIUMTEXT NULL,
  certificate_ids_body MEDIUMTEXT NULL,
  composite_schedule_body MEDIUMTEXT NULL,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp_local_authorization (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  id_tag VARCHAR(20) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  info_body TEXT NOT NULL,
  PRIMARY KEY(station_id,id_tag)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp_profile_assignment (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  connector_id INT NOT NULL,
  profile_id INT NOT NULL,
  purpose VARCHAR(32) NOT NULL,
  stack_level INT NOT NULL,
  transaction_id INT NULL,
  profile_body MEDIUMTEXT NOT NULL,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(station_id,connector_id,profile_id),
  UNIQUE KEY profile_stack(station_id,connector_id,purpose,stack_level)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp_security_event (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  event_type VARCHAR(64) NOT NULL,
  event_timestamp DATETIME(6) NOT NULL,
  technical_info TEXT NULL,
  received_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  INDEX security_station(station_id,event_timestamp)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp_certificate_request (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  request_body TEXT NOT NULL,
  state VARCHAR(32) NOT NULL DEFAULT 'PendingReview',
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  INDEX certificate_station(station_id,state)
) ENGINE=InnoDB;
