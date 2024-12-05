#!/bin/bash

# Load the launchd service
if [ -f /Library/LaunchDaemons/com.finden.fused-nfs.plist ]; then
    launchctl load -w /Library/LaunchDaemons/com.finden.fused-nfs.plist
fi

# Set up initial configuration
if [ ! -f /etc/fused-nfs/config.yaml ]; then
    mkdir -p /etc/fused-nfs
    cat > /etc/fused-nfs/config.yaml << EOF
exports:
  - path: /var/lib/fused-nfs/exports
    options: rw,async,no_root_squash

logging:
  level: info
  path: /var/log/fused-nfs/service.log

network:
  port: 2049
  bind_address: 0.0.0.0
EOF
    chmod 644 /etc/fused-nfs/config.yaml
fi

# Start the service
launchctl start com.finden.fused-nfs 