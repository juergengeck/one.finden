#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include "recovery_metrics.hpp"

namespace fused {

class MetricsVisualizer {
public:
    static std::string generate_html_report(const RecoveryMetrics& metrics) {
        std::stringstream ss;
        ss << R"(
<!DOCTYPE html>
<html>
<head>
    <title>Recovery Metrics Report</title>
    <style>
        body { font-family: sans-serif; margin: 20px; }
        table { border-collapse: collapse; }
        td, th { padding: 8px; border: 1px solid #ddd; }
        th { background-color: #f5f5f5; }
    </style>
</head>
<body>
    <h1>Recovery Metrics Report</h1>
    <div class="metrics">
        <table>
            <tr><td>Recovery Attempts:</td><td>)" << metrics.recovery_attempts << "</td></tr>"
            "<tr><td>Recovery Successes:</td><td>" << metrics.recovery_successes << "</td></tr>"
            "<tr><td>Recovery Failures:</td><td>" << metrics.recovery_failures << "</td></tr>"
            "<tr><td>Success Rate:</td><td>" << 
                (metrics.recovery_attempts > 0 ? 
                    100.0 * metrics.recovery_successes / metrics.recovery_attempts : 0.0) << "%</td></tr>"
            "<tr><td>Average Recovery Time:</td><td>" << 
                (metrics.recovery_attempts > 0 ? 
                    metrics.total_recovery_time_ms / metrics.recovery_attempts : 0) << "ms</td></tr>"
            "<tr><td>States Recovered:</td><td>" << metrics.states_recovered << "</td></tr>"
            "<tr><td>States Lost:</td><td>" << metrics.states_lost << "</td></tr>"
            "<tr><td>Operations Recovered:</td><td>" << metrics.operations_recovered << "</td></tr>"
            "<tr><td>Operations Failed:</td><td>" << metrics.operations_failed << "</td></tr>"
        R"(</table>
    </div>
</body>
</html>)";

        return ss.str();
    }
};

} // namespace fused 