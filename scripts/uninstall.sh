#!/bin/bash

# Stop and unload the service
launchctl stop com.finden.fused-nfs
launchctl unload -w /Library/LaunchDaemons/com.finden.fused-nfs.plist

# Remove files
rm -f /Library/LaunchDaemons/com.finden.fused-nfs.plist
rm -f /usr/local/lib/fused-nfs/post-install.sh
rm -rf /etc/fused-nfs
rm -rf /var/log/fused-nfs
rm -rf /var/lib/fused-nfs

# Remove binary
rm -f /usr/local/bin/fused-nfs 