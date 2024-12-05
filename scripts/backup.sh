#!/bin/bash
set -e

# Configuration
BACKUP_DIR="/var/lib/fused-nfs/backup"
CONFIG_DIR="/etc/fused-nfs"
STATE_DIR="/var/lib/fused-nfs"
RETENTION_DAYS=7

# Create backup name with timestamp
BACKUP_NAME="fused-nfs-backup-$(date +%Y%m%d%H%M%S).tar.gz"

# Create backup
tar czf ${BACKUP_DIR}/${BACKUP_NAME} \
    ${CONFIG_DIR} \
    ${STATE_DIR}/recovery \
    --exclude=${STATE_DIR}/backup

# Clean old backups
find ${BACKUP_DIR} -name "fused-nfs-backup-*.tar.gz" -mtime +${RETENTION_DAYS} -delete

echo "Backup completed: ${BACKUP_DIR}/${BACKUP_NAME}" 