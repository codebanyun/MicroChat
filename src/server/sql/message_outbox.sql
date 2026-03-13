CREATE DATABASE IF NOT EXISTS `MicroChat`;
USE `MicroChat`;

CREATE TABLE IF NOT EXISTS `message_outbox` (
  `id` BIGINT UNSIGNED NOT NULL PRIMARY KEY AUTO_INCREMENT,
  `event_id` varchar(64) NOT NULL,
  `message_id` varchar(64) NOT NULL,
  `channel` varchar(32) NOT NULL,
  `payload` TEXT NOT NULL,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `retry_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `next_retry_at` timestamp NULL,
  `last_error` varchar(255) NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  UNIQUE KEY `uniq_event_id` (`event_id`),
  UNIQUE KEY `uniq_message_channel` (`message_id`, `channel`),
  KEY `idx_status_next_retry` (`status`, `next_retry_at`)
) ENGINE=InnoDB;
