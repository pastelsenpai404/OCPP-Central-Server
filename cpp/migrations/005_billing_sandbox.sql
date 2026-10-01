-- This ledger stores simulated settlements only. No payment gateway is connected.
CREATE TABLE IF NOT EXISTS cpp_billing_sandbox (
  case_id VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  request_body MEDIUMTEXT NOT NULL,
  response_body MEDIUMTEXT NOT NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)
) ENGINE=InnoDB;
