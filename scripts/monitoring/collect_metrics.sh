#!/bin/bash
set -e

# Configuration
METRICS_DIR="/var/lib/fused-nfs/metrics"
PROMETHEUS_URL="http://localhost:9090"
RETENTION_DAYS=7

# Metrics to collect
declare -A METRICS=(
    ["operations"]="rate(fused_nfs_operations_total[5m])"
    ["errors"]="rate(fused_nfs_operation_errors_total[5m])"
    ["latency"]="histogram_quantile(0.99, rate(fused_nfs_operation_duration_seconds_bucket[5m]))"
    ["memory"]="process_resident_memory_bytes{job=\"fused-nfs\"}"
    ["cpu"]="rate(process_cpu_seconds_total{job=\"fused-nfs\"}[5m])"
)

# Create metrics directory if it doesn't exist
mkdir -p "$METRICS_DIR"

# Collect metrics
collect_metrics() {
    local timestamp=$(date +%Y%m%d%H%M%S)
    local metrics_file="${METRICS_DIR}/metrics_${timestamp}.json"

    echo "{" > "$metrics_file"
    echo "  \"timestamp\": \"$(date -Iseconds)\"," >> "$metrics_file"
    echo "  \"metrics\": {" >> "$metrics_file"

    local first=true
    for metric in "${!METRICS[@]}"; do
        if [ "$first" = true ]; then
            first=false
        else
            echo "," >> "$metrics_file"
        fi

        local query="${METRICS[$metric]}"
        local value=$(curl -s "${PROMETHEUS_URL}/api/v1/query" \
            --data-urlencode "query=${query}" | \
            jq -r '.data.result[0].value[1]')

        echo "    \"${metric}\": ${value}" >> "$metrics_file"
    done

    echo "  }" >> "$metrics_file"
    echo "}" >> "$metrics_file"

    # Export to Prometheus format
    local prom_file="${METRICS_DIR}/metrics_${timestamp}.prom"
    for metric in "${!METRICS[@]}"; do
        local value=$(jq -r ".metrics.${metric}" "$metrics_file")
        echo "fused_nfs_${metric} ${value}" >> "$prom_file"
    done
}

# Cleanup old metrics
cleanup_metrics() {
    find "$METRICS_DIR" -name "metrics_*.json" -mtime +${RETENTION_DAYS} -delete
    find "$METRICS_DIR" -name "metrics_*.prom" -mtime +${RETENTION_DAYS} -delete
}

# Main execution
collect_metrics
cleanup_metrics 