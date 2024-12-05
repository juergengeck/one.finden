#pragma once
#include <string>
#include <memory>
#include <sstream>
#include "alert_manager.hpp"
#include "logger.hpp"

namespace fused {

// Base class for alert handlers
class AlertHandler {
public:
    virtual ~AlertHandler() = default;
    virtual void handle(const Alert& alert) = 0;
};

// Log file alert handler
class LogAlertHandler : public AlertHandler {
public:
    void handle(const Alert& alert) override {
        std::string severity;
        switch (alert.severity) {
            case AlertSeverity::INFO:     severity = "INFO"; break;
            case AlertSeverity::WARNING:  severity = "WARNING"; break;
            case AlertSeverity::ERROR:    severity = "ERROR"; break;
            case AlertSeverity::CRITICAL: severity = "CRITICAL"; break;
        }

        std::stringstream metrics_str;
        for (const auto& [key, value] : alert.metrics) {
            metrics_str << " " << key << "=" << value;
        }

        LOG_INFO("[ALERT][{}] {}: {}{}", 
            severity, alert.id, alert.message, metrics_str.str());
    }
};

// Email alert handler
class EmailAlertHandler : public AlertHandler {
public:
    explicit EmailAlertHandler(const std::string& recipient)
        : recipient_(recipient) {}

    void handle(const Alert& alert) override {
        if (alert.severity >= AlertSeverity::WARNING) {
            send_email(alert);
        }
    }

private:
    std::string recipient_;

    void send_email(const Alert& alert) {
        // Implementation would use SMTP library or system mail command
        LOG_INFO("Would send email to {} about alert: {}", recipient_, alert.id);
    }
};

// Webhook alert handler
class WebhookAlertHandler : public AlertHandler {
public:
    explicit WebhookAlertHandler(const std::string& url)
        : webhook_url_(url) {}

    void handle(const Alert& alert) override {
        send_webhook(alert);
    }

private:
    std::string webhook_url_;

    void send_webhook(const Alert& alert) {
        // Implementation would use HTTP client library
        LOG_INFO("Would send webhook to {} for alert: {}", webhook_url_, alert.id);
    }
};

// Alert handler factory
class AlertHandlerFactory {
public:
    static std::unique_ptr<AlertHandler> create_handler(const std::string& type, 
                                                      const std::string& config) {
        if (type == "log") {
            return std::make_unique<LogAlertHandler>();
        } else if (type == "email") {
            return std::make_unique<EmailAlertHandler>(config);
        } else if (type == "webhook") {
            return std::make_unique<WebhookAlertHandler>(config);
        }
        return nullptr;
    }
};

} // namespace fuse_t 