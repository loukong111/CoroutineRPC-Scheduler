CREATE DATABASE IF NOT EXISTS corpcron DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE corpcron;

CREATE TABLE IF NOT EXISTS tasks (
    id VARCHAR(64) PRIMARY KEY,
    cron_expr VARCHAR(100) NOT NULL,
    params TEXT,
    handler VARCHAR(100) NOT NULL,
    status TINYINT NOT NULL DEFAULT 1,
    next_run_at DATETIME NULL,
    last_run_at DATETIME NULL,
    retry_count INT NOT NULL DEFAULT 0,
    max_retries INT NOT NULL DEFAULT 3,
    current_execution_id VARCHAR(128) NULL,
    running_node VARCHAR(128) NULL,
    started_at DATETIME NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_tasks_status_next_run (status, next_run_at),
    INDEX idx_tasks_status_started_at (status, started_at)
);

CREATE TABLE IF NOT EXISTS task_history (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    execution_id VARCHAR(128) NULL,
    task_id VARCHAR(64) NOT NULL,
    exec_node VARCHAR(128),
    success TINYINT NOT NULL DEFAULT 0,
    result TEXT,
    error TEXT,
    start_time DATETIME,
    end_time DATETIME,
    UNIQUE KEY uk_task_history_execution_id (execution_id),
    INDEX idx_task_history_task_id (task_id),
    INDEX idx_task_history_start_time (start_time)
);
