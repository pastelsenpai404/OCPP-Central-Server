-- Version and transport are part of replay identity; never share financial replies across protocols.
CREATE TABLE IF NOT EXISTS cpp_legacy_replay (
  station_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  protocol VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  message_id VARCHAR(128) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  request_body MEDIUMTEXT NOT NULL,
  response_body MEDIUMTEXT NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY(station_id,protocol,message_id)
) ENGINE=InnoDB;
