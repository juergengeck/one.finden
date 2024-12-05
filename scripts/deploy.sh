#!/bin/bash
set -e

# Configuration
ENV=${1:-production}
CONFIG_SOURCE="config/${ENV}"
REMOTE_HOST=${2:-""}
REMOTE_USER=${3:-"root"}

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

# Check arguments
if [ -z "$REMOTE_HOST" ]; then
    error "Usage: $0 [environment] [remote-host] [remote-user]"
fi

# Build project
log "Building project..."
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..

# Package files
log "Packaging files..."
PACKAGE="fused-nfs-${ENV}-$(date +%Y%m%d%H%M%S).tar.gz"
tar czf $PACKAGE \
    build/bin/* \
    config/${ENV}/* \
    etc/systemd/system/* \
    scripts/install.sh \
    man/man*/*

# Copy to remote host
log "Copying to remote host..."
scp $PACKAGE ${REMOTE_USER}@${REMOTE_HOST}:/tmp/

# Run installation
log "Running installation..."
ssh ${REMOTE_USER}@${REMOTE_HOST} "cd /tmp && \
    tar xzf $PACKAGE && \
    ./scripts/install.sh && \
    rm -rf $PACKAGE"

log "Deployment complete!" 