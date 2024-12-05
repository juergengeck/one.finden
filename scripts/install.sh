#!/bin/bash
set -e

# Configuration
INSTALL_PREFIX="/usr"
CONFIG_DIR="/etc/fused-nfs"
LOG_DIR="/var/log/fused-nfs"
STATE_DIR="/var/lib/fused-nfs"
RUN_DIR="/run/fused-nfs"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

# Logging function
log() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
    exit 1
}

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    error "Please run as root"
fi

# Create directories
log "Creating directories..."
mkdir -p \
    ${CONFIG_DIR}/{config.d,security.d} \
    ${LOG_DIR} \
    ${STATE_DIR}/{recovery,backup} \
    ${RUN_DIR}

# Set permissions
log "Setting permissions..."
chmod 750 ${CONFIG_DIR} ${STATE_DIR}
chmod 755 ${LOG_DIR} ${RUN_DIR}
chown -R root:root ${CONFIG_DIR}
chown -R fused-nfs:fused-nfs ${LOG_DIR} ${STATE_DIR} ${RUN_DIR}

# Install binaries
log "Installing binaries..."
install -m 755 build/bin/fused-nfs ${INSTALL_PREFIX}/sbin/
install -m 755 build/bin/fused-nfs-admin ${INSTALL_PREFIX}/sbin/
install -m 755 build/bin/fused-nfs-recovery ${INSTALL_PREFIX}/sbin/

# Install configuration files
log "Installing configuration files..."
install -m 640 config/config.yaml ${CONFIG_DIR}/
install -m 640 config/security.yaml ${CONFIG_DIR}/
install -m 640 config/logging.yaml ${CONFIG_DIR}/

# Install systemd services
log "Installing systemd services..."
install -m 644 etc/systemd/system/fused-nfs.service /etc/systemd/system/
install -m 644 etc/systemd/system/fused-nfs-monitoring.service /etc/systemd/system/
install -m 644 etc/systemd/system/fused-nfs-maintenance.service /etc/systemd/system/
install -m 644 etc/systemd/system/fused-nfs-maintenance.timer /etc/systemd/system/

# Install man pages
log "Installing man pages..."
for page in man/man*/*; do
    section=$(basename $(dirname $page))
    install -m 644 $page /usr/share/man/${section}/
done

# Reload systemd
log "Reloading systemd..."
systemctl daemon-reload

# Create system user
log "Creating system user..."
useradd -r -s /sbin/nologin -d /var/lib/fused-nfs -M fused-nfs 2>/dev/null || true

# Final setup
log "Running final setup..."
systemctl enable fused-nfs.service
systemctl enable fused-nfs-monitoring.service
systemctl enable fused-nfs-maintenance.timer

log "Installation complete!"
log "Please edit ${CONFIG_DIR}/config.yaml before starting the service" 