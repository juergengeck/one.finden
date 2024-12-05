#!/bin/bash
set -e

# Configuration
CONFIG_FILE="/etc/fused-nfs/monitoring/healthcheck.yml"
STATE_FILE="/var/lib/fused-nfs/monitoring/health_state.json"
ALERT_SCRIPT="/usr/sbin/fused-nfs-alert"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Logging function
log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $1" | tee -a /var/log/fused-nfs/health.log
}

# Check system resources
check_resources() {
    CPU_USAGE=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | cut -d. -f1)
    MEM_USAGE=$(free | grep Mem | awk '{print $3/$2 * 100.0}' | cut -d. -f1)
    DISK_USAGE=$(df -h / | tail -1 | awk '{print $5}' | cut -d% -f1)

    local status="OK"
    [[ $CPU_USAGE -gt 80 ]] && status="WARN"
    [[ $MEM_USAGE -gt 80 ]] && status="WARN"
    [[ $DISK_USAGE -gt 90 ]] && status="WARN"

    echo "$status"
}

# Check service status
check_services() {
    local status="OK"
    systemctl is-active --quiet fused-nfs || status="FAIL"
    systemctl is-active --quiet fused-nfs-monitoring || status="FAIL"
    echo "$status"
}

# Check operation metrics
check_metrics() {
    local status="OK"
    # Query Prometheus for metrics
    ERROR_RATE=$(curl -s "http://localhost:9090/api/v1/query" \
        --data-urlencode 'query=rate(fused_nfs_operation_errors_total[5m])' | \
        jq -r '.data.result[0].value[1]')
    
    [[ $(echo "$ERROR_RATE > 0.05" | bc -l) == 1 ]] && status="WARN"
    echo "$status"
}

# Check recovery status
check_recovery() {
    local status="OK"
    PENDING=$(curl -s "http://localhost:8080/metrics" | grep pending_recoveries | cut -d' ' -f2)
    [[ $PENDING -gt 0 ]] && status="WARN"
    echo "$status"
}

# Main health check function
perform_health_check() {
    local resource_status=$(check_resources)
    local service_status=$(check_services)
    local metrics_status=$(check_metrics)
    local recovery_status=$(check_recovery)

    # Generate health report
    cat > "$STATE_FILE" <<EOF
{
    "timestamp": "$(date -Iseconds)",
    "status": {
        "resources": "$resource_status",
        "services": "$service_status",
        "metrics": "$metrics_status",
        "recovery": "$recovery_status"
    }
}
EOF

    # Handle alerts
    if [[ $service_status == "FAIL" ]]; then
        $ALERT_SCRIPT --severity critical --message "Service failure detected"
    elif [[ $resource_status == "WARN" || $metrics_status == "WARN" || $recovery_status == "WARN" ]]; then
        $ALERT_SCRIPT --severity warning --message "System warnings detected"
    fi

    # Log status
    log "Health check completed - Resources: $resource_status, Services: $service_status, Metrics: $metrics_status, Recovery: $recovery_status"
}

# Run health check
perform_health_check 