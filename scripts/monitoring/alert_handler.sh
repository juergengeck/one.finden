#!/bin/bash
set -e

# Configuration
ALERT_LOG="/var/log/fused-nfs/alerts.log"
ALERT_SCRIPT="/usr/sbin/fused-nfs-alert"
NOTIFICATION_CONFIG="/etc/fused-nfs/monitoring/notifications.yml"

# Load notification configuration
source "$NOTIFICATION_CONFIG"

# Handle alert
handle_alert() {
    local severity="$1"
    local message="$2"
    local timestamp=$(date -Iseconds)

    # Log alert
    echo "$timestamp [$severity] $message" >> "$ALERT_LOG"

    # Send notifications based on severity
    case "$severity" in
        critical)
            send_email "$ADMIN_EMAIL" "CRITICAL: $message"
            send_slack "$SLACK_WEBHOOK" "🚨 *CRITICAL*: $message"
            ;;
        warning)
            send_slack "$SLACK_WEBHOOK" "⚠️ *WARNING*: $message"
            ;;
        info)
            send_slack "$SLACK_WEBHOOK" "ℹ️ *INFO*: $message"
            ;;
    esac
}

# Notification functions
send_email() {
    local email="$1"
    local message="$2"
    echo "$message" | mail -s "FUSE-NFS Alert" "$email"
}

send_slack() {
    local webhook="$1"
    local message="$2"
    curl -X POST -H 'Content-type: application/json' \
        --data "{\"text\":\"$message\"}" "$webhook"
}

# Main execution
severity="$1"
message="$2"

if [ -z "$severity" ] || [ -z "$message" ]; then
    echo "Usage: $0 <severity> <message>"
    exit 1
fi

handle_alert "$severity" "$message" 