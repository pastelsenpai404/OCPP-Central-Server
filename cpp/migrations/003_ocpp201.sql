CREATE TABLE IF NOT EXISTS cpp201_transaction (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  transaction_id VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  evse_id INT NULL,
  connector_id INT NULL,
  token_body TEXT NULL,
  charging_state VARCHAR(32) NULL,
  start_timestamp DATETIME(6) NULL,
  end_timestamp DATETIME(6) NULL,
  last_seq_no INT NOT NULL DEFAULT -1,
  latest_body MEDIUMTEXT NOT NULL,
  PRIMARY KEY(station_id,transaction_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_transaction_event (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  transaction_id VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  seq_no INT NOT NULL,
  event_type VARCHAR(16) NOT NULL,
  event_timestamp DATETIME(6) NOT NULL,
  event_body MEDIUMTEXT NOT NULL,
  received_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(station_id,transaction_id,seq_no)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_replay (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  message_id VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  request_body MEDIUMTEXT NOT NULL,
  response_body MEDIUMTEXT NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(station_id,message_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_evse_status (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  evse_id INT NOT NULL,
  connector_id INT NOT NULL,
  connector_status VARCHAR(32) NOT NULL,
  status_timestamp DATETIME(6) NOT NULL,
  PRIMARY KEY(station_id,evse_id,connector_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_report (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  action VARCHAR(64) NOT NULL,
  request_id INT NULL,
  seq_no INT NULL,
  final_chunk TINYINT NOT NULL DEFAULT 1,
  report_body MEDIUMTEXT NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  UNIQUE KEY report_chunk(station_id,action,request_id,seq_no),
  INDEX report_station(station_id,action,id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_meter (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  evse_id INT NOT NULL,
  transaction_id VARCHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
  sample_timestamp DATETIME(6) NOT NULL,
  meter_body MEDIUMTEXT NOT NULL,
  INDEX meter_station(station_id,evse_id,sample_timestamp)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_id_token (
  token VARCHAR(36) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
  token_type VARCHAR(24) NOT NULL,
  status VARCHAR(32) NOT NULL,
  expiry_timestamp DATETIME(6) NULL,
  group_token_body TEXT NULL,
  PRIMARY KEY(token,token_type)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_device_variable (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  component_name VARCHAR(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  component_instance VARCHAR(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',
  evse_id INT NOT NULL DEFAULT 0,
  connector_id INT NOT NULL DEFAULT 0,
  variable_name VARCHAR(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  variable_instance VARCHAR(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',
  attribute_type VARCHAR(16) NOT NULL DEFAULT 'Actual',
  value TEXT NULL,
  metadata_body MEDIUMTEXT NULL,
  updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6),
  PRIMARY KEY(station_id,component_name,component_instance,evse_id,connector_id,variable_name,variable_instance,attribute_type)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_local_authorization (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  token VARCHAR(36) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
  token_type VARCHAR(24) NOT NULL,
  info_body TEXT NOT NULL,
  PRIMARY KEY(station_id,token,token_type)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_reservation (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  reservation_id INT NOT NULL,
  evse_id INT NULL,
  expiry_timestamp DATETIME(6) NOT NULL,
  request_body TEXT NOT NULL,
  state VARCHAR(32) NOT NULL DEFAULT 'Pending',
  PRIMARY KEY(station_id,reservation_id)
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS cpp201_profile (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  evse_id INT NOT NULL,
  profile_id INT NOT NULL,
  purpose VARCHAR(32) NOT NULL,
  stack_level INT NOT NULL,
  profile_body MEDIUMTEXT NOT NULL,
  PRIMARY KEY(station_id,evse_id,profile_id),
  UNIQUE KEY profile_stack(station_id,evse_id,purpose,stack_level)
) ENGINE=InnoDB;
