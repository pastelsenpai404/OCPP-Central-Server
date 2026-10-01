-- Schema definitions only, extracted from the user-provided legacy database reference dumps.
-- No historical data, credentials or migration records are included.

CREATE TABLE IF NOT EXISTS `address` (
  `address_pk` int(11) NOT NULL AUTO_INCREMENT,
  `street` varchar(1000) DEFAULT NULL,
  `house_number` varchar(255) DEFAULT NULL,
  `zip_code` varchar(255) DEFAULT NULL,
  `city` varchar(255) DEFAULT NULL,
  `country` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`address_pk`)
) ENGINE=InnoDB AUTO_INCREMENT=1723 DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

CREATE TABLE IF NOT EXISTS `charge_box` (
  `charge_box_pk` int(11) NOT NULL AUTO_INCREMENT,
  `charge_box_id` varchar(255) NOT NULL,
  `endpoint_address` varchar(255) DEFAULT NULL,
  `ocpp_protocol` varchar(255) DEFAULT NULL,
  `registration_status` varchar(255) NOT NULL DEFAULT 'Accepted',
  `charge_point_vendor` varchar(255) DEFAULT NULL,
  `charge_point_model` varchar(255) DEFAULT NULL,
  `charge_point_serial_number` varchar(255) DEFAULT NULL,
  `charge_box_serial_number` varchar(255) DEFAULT NULL,
  `fw_version` varchar(255) DEFAULT NULL,
  `fw_update_status` varchar(255) DEFAULT NULL,
  `fw_update_timestamp` timestamp(6) NULL DEFAULT NULL,
  `iccid` varchar(255) DEFAULT NULL,
  `imsi` varchar(255) DEFAULT NULL,
  `meter_type` varchar(255) DEFAULT NULL,
  `meter_serial_number` varchar(255) DEFAULT NULL,
  `diagnostics_status` varchar(255) DEFAULT NULL,
  `diagnostics_timestamp` timestamp(6) NULL DEFAULT NULL,
  `last_heartbeat_timestamp` timestamp(6) NULL DEFAULT NULL,
  `description` mediumtext DEFAULT NULL,
  `note` mediumtext DEFAULT NULL,
  `location_latitude` decimal(11,8) DEFAULT NULL,
  `location_longitude` decimal(11,8) DEFAULT NULL,
  `address_pk` int(11) DEFAULT NULL,
  `admin_address` varchar(255) DEFAULT NULL,
  `insert_connector_status_after_transaction_msg` tinyint(1) DEFAULT 1,
  PRIMARY KEY (`charge_box_pk`),
  UNIQUE KEY `chargeBoxId_UNIQUE` (`charge_box_id`),
  KEY `chargebox_op_ep_idx` (`ocpp_protocol`,`endpoint_address`),
  KEY `FK_charge_box_address_apk` (`address_pk`),
  CONSTRAINT `FK_charge_box_address_apk` FOREIGN KEY (`address_pk`) REFERENCES `address` (`address_pk`) ON DELETE SET NULL ON UPDATE NO ACTION
) ENGINE=InnoDB AUTO_INCREMENT=301 DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

CREATE TABLE IF NOT EXISTS `ocpp_tag` (
  `ocpp_tag_pk` int(11) NOT NULL AUTO_INCREMENT,
  `id_tag` varchar(255) NOT NULL,
  `parent_id_tag` varchar(255) DEFAULT NULL,
  `expiry_date` timestamp(6) NULL DEFAULT NULL,
  `max_active_transaction_count` int(11) NOT NULL DEFAULT 1,
  `note` mediumtext DEFAULT NULL,
  PRIMARY KEY (`ocpp_tag_pk`),
  UNIQUE KEY `idTag_UNIQUE` (`id_tag`),
  KEY `user_expiryDate_idx` (`expiry_date`),
  KEY `FK_ocpp_tag_parent_id_tag` (`parent_id_tag`)
) ENGINE=InnoDB AUTO_INCREMENT=22277 DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

CREATE TABLE IF NOT EXISTS `connector` (
  `connector_pk` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `charge_box_id` varchar(255) NOT NULL,
  `connector_id` int(11) NOT NULL,
  PRIMARY KEY (`connector_pk`),
  UNIQUE KEY `connector_pk_UNIQUE` (`connector_pk`),
  UNIQUE KEY `connector_cbid_cid_UNIQUE` (`charge_box_id`,`connector_id`),
  CONSTRAINT `FK_connector_charge_box` FOREIGN KEY (`charge_box_id`) REFERENCES `charge_box` (`charge_box_id`) ON DELETE CASCADE
) ENGINE=InnoDB AUTO_INCREMENT=2061628 DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

CREATE TABLE IF NOT EXISTS `connector_status` (
  `connector_pk` int(11) unsigned NOT NULL,
  `status_timestamp` timestamp(6) NULL DEFAULT NULL COMMENT '17 -> 10  ไทย',
  `status` varchar(255) DEFAULT NULL,
  `error_code` varchar(255) DEFAULT NULL,
  `error_info` varchar(255) DEFAULT NULL,
  `vendor_id` varchar(255) DEFAULT NULL,
  `vendor_error_code` varchar(255) DEFAULT NULL,
  KEY `FK_cs_pk_idx` (`connector_pk`),
  KEY `connector_status_cpk_st_idx` (`connector_pk`,`status_timestamp`),
  CONSTRAINT `FK_cs_pk` FOREIGN KEY (`connector_pk`) REFERENCES `connector` (`connector_pk`) ON DELETE CASCADE ON UPDATE NO ACTION
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

CREATE TABLE IF NOT EXISTS `transaction_start` (
  `transaction_pk` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `event_timestamp` timestamp(6) NOT NULL DEFAULT current_timestamp(6),
  `connector_pk` int(11) unsigned NOT NULL,
  `id_tag` varchar(255) NOT NULL,
  `start_timestamp` timestamp(6) NULL DEFAULT NULL,
  `start_value` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`transaction_pk`),
  UNIQUE KEY `transaction_pk_UNIQUE` (`transaction_pk`),
  KEY `idTag_idx` (`id_tag`),
  KEY `connector_pk_idx` (`connector_pk`),
  KEY `transaction_start_idx` (`start_timestamp`),
  CONSTRAINT `FK_connector_pk_t` FOREIGN KEY (`connector_pk`) REFERENCES `connector` (`connector_pk`) ON DELETE CASCADE ON UPDATE NO ACTION,
  CONSTRAINT `FK_transaction_ocpp_tag_id_tag` FOREIGN KEY (`id_tag`) REFERENCES `ocpp_tag` (`id_tag`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB AUTO_INCREMENT=19967 DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

CREATE TABLE IF NOT EXISTS `transaction_stop` (
  `transaction_pk` int(10) unsigned NOT NULL,
  `event_timestamp` timestamp(6) NOT NULL DEFAULT current_timestamp(6),
  `event_actor` enum('station','manual') DEFAULT NULL,
  `stop_timestamp` timestamp(6) NOT NULL DEFAULT current_timestamp(6),
  `stop_value` varchar(255) NOT NULL,
  `stop_reason` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`transaction_pk`,`event_timestamp`),
  CONSTRAINT `FK_transaction_stop_transaction_pk` FOREIGN KEY (`transaction_pk`) REFERENCES `transaction_start` (`transaction_pk`) ON DELETE CASCADE ON UPDATE NO ACTION
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `connector_meter_value` (
  `connector_pk` int(11) unsigned NOT NULL,
  `transaction_pk` int(10) unsigned DEFAULT NULL,
  `value_timestamp` timestamp(6) NULL DEFAULT NULL,
  `value` text DEFAULT NULL,
  `reading_context` varchar(255) DEFAULT NULL,
  `format` varchar(255) DEFAULT NULL,
  `measurand` varchar(255) DEFAULT NULL,
  `location` varchar(255) DEFAULT NULL,
  `unit` varchar(255) DEFAULT NULL,
  `phase` varchar(255) DEFAULT NULL,
  KEY `FK_cm_pk_idx` (`connector_pk`),
  KEY `FK_tid_cm_idx` (`transaction_pk`),
  KEY `cmv_value_timestamp_idx` (`value_timestamp`),
  CONSTRAINT `FK_pk_cm` FOREIGN KEY (`connector_pk`) REFERENCES `connector` (`connector_pk`) ON DELETE CASCADE ON UPDATE NO ACTION,
  CONSTRAINT `FK_tid_cm` FOREIGN KEY (`transaction_pk`) REFERENCES `transaction_start` (`transaction_pk`) ON DELETE SET NULL ON UPDATE NO ACTION
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

CREATE TABLE IF NOT EXISTS `reservation` (
  `reservation_pk` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `connector_pk` int(11) unsigned NOT NULL,
  `transaction_pk` int(10) unsigned DEFAULT NULL,
  `id_tag` varchar(255) NOT NULL,
  `start_datetime` datetime DEFAULT NULL,
  `expiry_datetime` datetime DEFAULT NULL,
  `status` varchar(255) NOT NULL,
  PRIMARY KEY (`reservation_pk`),
  UNIQUE KEY `reservation_pk_UNIQUE` (`reservation_pk`),
  UNIQUE KEY `transaction_pk_UNIQUE` (`transaction_pk`),
  KEY `FK_idTag_r_idx` (`id_tag`),
  KEY `reservation_start_idx` (`start_datetime`),
  KEY `reservation_expiry_idx` (`expiry_datetime`),
  KEY `reservation_status_idx` (`status`),
  KEY `FK_connector_pk_reserv_idx` (`connector_pk`),
  CONSTRAINT `FK_connector_pk_reserv` FOREIGN KEY (`connector_pk`) REFERENCES `connector` (`connector_pk`) ON DELETE CASCADE ON UPDATE NO ACTION,
  CONSTRAINT `FK_reservation_ocpp_tag_id_tag` FOREIGN KEY (`id_tag`) REFERENCES `ocpp_tag` (`id_tag`) ON DELETE CASCADE ON UPDATE NO ACTION,
  CONSTRAINT `FK_transaction_pk_r` FOREIGN KEY (`transaction_pk`) REFERENCES `transaction_start` (`transaction_pk`) ON DELETE NO ACTION ON UPDATE NO ACTION
) ENGINE=InnoDB AUTO_INCREMENT=27 DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

CREATE TABLE IF NOT EXISTS `settings` (
  `app_id` varchar(40) NOT NULL,
  `heartbeat_interval_in_seconds` int(11) DEFAULT NULL,
  `hours_to_expire` int(11) DEFAULT NULL,
  `mail_enabled` tinyint(1) DEFAULT 0,
  `mail_host` varchar(255) DEFAULT NULL,
  `mail_username` varchar(255) DEFAULT NULL,
  `mail_password` varchar(255) DEFAULT NULL,
  `mail_from` varchar(255) DEFAULT NULL,
  `mail_protocol` varchar(255) DEFAULT 'smtp',
  `mail_port` int(11) DEFAULT 25,
  `mail_recipients` text DEFAULT NULL COMMENT 'comma separated list of email addresses',
  `notification_features` text DEFAULT NULL COMMENT 'comma separated list',
  PRIMARY KEY (`app_id`),
  UNIQUE KEY `settings_id_UNIQUE` (`app_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;
