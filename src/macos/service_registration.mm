#include "service_registration.hpp"
#include <Foundation/Foundation.h>
#include <ServiceManagement/ServiceManagement.h>
#include <Security/Security.h>
#include <dns_sd.h>

namespace fused {

class ServiceRegistration::Impl {
public:
    bool registered{false};
    DNSServiceRef dns_ref{nullptr};
    NSString* service_id{nullptr};
    
    ~Impl() {
        if (dns_ref) {
            DNSServiceRefDeallocate(dns_ref);
        }
    }
};

ServiceRegistration::ServiceRegistration() : impl_(std::make_unique<Impl>()) {}
ServiceRegistration::~ServiceRegistration() = default;

bool ServiceRegistration::register_service(uint16_t port) {
    @autoreleasepool {
        // Create service identifier
        impl_->service_id = @"com.finden.fused-nfs";

        // Register with each subsystem
        if (!register_with_launchd() ||
            !register_with_nfs_registry() ||
            !register_with_bonjour() ||
            !setup_security_policies()) {
            return false;
        }

        impl_->registered = true;
        LOG_INFO("Service registered successfully");
        return true;
    }
}

bool ServiceRegistration::register_with_launchd() {
    @autoreleasepool {
        // Create service description
        NSDictionary* service_info = @{
            @"Label": impl_->service_id,
            @"MachServices": @{impl_->service_id: @YES},
            @"Program": @"/usr/local/bin/fused-nfs",
            @"ProgramArguments": @[@"/usr/local/bin/fused-nfs", @"--daemon"],
            @"KeepAlive": @YES,
            @"RunAtLoad": @YES
        };

        CFErrorRef error = nullptr;
        if (!SMJobSubmit(kSMDomainSystemLaunchd, 
                        (__bridge CFDictionaryRef)service_info,
                        nullptr, &error)) {
            if (error) {
                LOG_ERROR("Failed to register with launchd: {}",
                    [(__bridge NSError*)error localizedDescription].UTF8String);
                CFRelease(error);
            }
            return false;
        }

        LOG_INFO("Registered with launchd");
        return true;
    }
}

bool ServiceRegistration::register_with_nfs_registry() {
    // Register with macOS NFS registry
    // This allows the system to recognize our service as an NFS provider
    @autoreleasepool {
        NSFileManager* fm = [NSFileManager defaultManager];
        NSString* nfs_dir = @"/etc/nfs.conf.d";
        NSString* conf_file = [nfs_dir stringByAppendingPathComponent:@"fused-nfs.conf"];

        // Create config directory if needed
        if (![fm fileExistsAtPath:nfs_dir]) {
            NSError* error = nil;
            if (![fm createDirectoryAtPath:nfs_dir 
                  withIntermediateDirectories:YES 
                  attributes:nil 
                  error:&error]) {
                LOG_ERROR("Failed to create NFS config directory: {}",
                    error.localizedDescription.UTF8String);
                return false;
            }
        }

        // Write config file
        NSString* config = @"[fused-nfs]\n"
                          @"port=2049\n"
                          @"threads=8\n"
                          @"vers4=yes\n"
                          @"sec=sys,krb5\n";

        NSError* error = nil;
        if (![config writeToFile:conf_file 
                    atomically:YES 
                    encoding:NSUTF8StringEncoding 
                    error:&error]) {
            LOG_ERROR("Failed to write NFS config: {}", 
                error.localizedDescription.UTF8String);
            return false;
        }

        LOG_INFO("Registered with NFS registry");
        return true;
    }
}

bool ServiceRegistration::register_with_bonjour() {
    // Register NFS service with Bonjour for discovery
    DNSServiceErrorType err = DNSServiceRegister(&impl_->dns_ref,
        0,                  // Default flags
        0,                  // All interfaces
        "Fused NFS",        // Service name
        "_nfs._tcp",        // Service type
        "",                 // Domain (default)
        nullptr,            // Host (default)
        htons(2049),        // Port
        0,                  // TXT record length
        nullptr,            // TXT record data
        nullptr,            // Callback
        nullptr            // Context
    );

    if (err != kDNSServiceErr_NoError) {
        LOG_ERROR("Failed to register with Bonjour: {}", err);
        return false;
    }

    LOG_INFO("Registered with Bonjour");
    return true;
}

bool ServiceRegistration::setup_security_policies() {
    @autoreleasepool {
        // Set up security policies for the service
        SecRequirementRef requirement = nullptr;
        CFErrorRef error = nullptr;

        // Create security requirement
        if (SecRequirementCreateWithString(
                CFSTR("anchor apple generic and identifier \"com.finden.fused-nfs\""),
                kSecCSDefaultFlags,
                &requirement) != errSecSuccess) {
            LOG_ERROR("Failed to create security requirement");
            return false;
        }

        // Apply security policy
        SecCodeRef code = nullptr;
        if (SecCodeCopySelf(kSecCSDefaultFlags, &code) == errSecSuccess) {
            if (SecCodeCheckValidity(code, kSecCSDefaultFlags, requirement) != errSecSuccess) {
                LOG_ERROR("Failed security policy check");
                CFRelease(code);
                CFRelease(requirement);
                return false;
            }
            CFRelease(code);
        }
        CFRelease(requirement);

        LOG_INFO("Security policies configured");
        return true;
    }
}

bool ServiceRegistration::unregister_service() {
    @autoreleasepool {
        if (!impl_->registered) {
            return true;
        }

        // Remove from launchd
        CFErrorRef error = nullptr;
        if (!SMJobRemove(kSMDomainSystemLaunchd,
                        (__bridge CFStringRef)impl_->service_id,
                        nullptr, true, &error)) {
            if (error) {
                LOG_ERROR("Failed to unregister from launchd: {}",
                    [(__bridge NSError*)error localizedDescription].UTF8String);
                CFRelease(error);
            }
            return false;
        }

        // Unregister from Bonjour
        if (impl_->dns_ref) {
            DNSServiceRefDeallocate(impl_->dns_ref);
            impl_->dns_ref = nullptr;
        }

        // Remove NFS config
        NSError* ns_error = nil;
        NSString* conf_file = @"/etc/nfs.conf.d/fused-nfs.conf";
        if (![[NSFileManager defaultManager] removeItemAtPath:conf_file 
                                                      error:&ns_error]) {
            LOG_ERROR("Failed to remove NFS config: {}", 
                ns_error.localizedDescription.UTF8String);
        }

        impl_->registered = false;
        LOG_INFO("Service unregistered");
        return true;
    }
}

bool ServiceRegistration::is_registered() const {
    return impl_->registered;
}

std::string ServiceRegistration::get_status() const {
    @autoreleasepool {
        // Check service status with launchd
        CFErrorRef error = nullptr;
        CFDictionaryRef job_dict = SMJobCopyDictionary(kSMDomainSystemLaunchd,
                                                      (__bridge CFStringRef)impl_->service_id);
        if (!job_dict) {
            return "Not registered";
        }

        NSDictionary* status = (__bridge NSDictionary*)job_dict;
        NSString* state = status[@"PID"] ? @"Running" : @"Stopped";
        CFRelease(job_dict);

        return [state UTF8String];
    }
}

} // namespace fused 