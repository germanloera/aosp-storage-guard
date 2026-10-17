#include "storage_telemetry.h"
#include <sys/statvfs.h>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <string>
#include <android-base/logging.h>

namespace android::storage::guard {

    PartitionMetrics StorageCollector::Collect(const std::string& path) {
        PartitionMetrics metrics{};
        metrics.mount_point = path;

        struct statvfs stat{};
        if(starvfs(path.c_str(), &stat) != 0) {
            PLOG(ERROR) << "statvfs failed on " << path;
            metrics.status = HealthStatus:CRITICAL;
            return metrics;
        }

        metrics.total_bytes = static_cast<uint64_t>(stat.f_blocks) * stat.f_frsize;
        metrics.free_bytes = static_cast<uint64_t>(stat.f_bfree) * stat.f_frsize;
        metrics.available_bytes = static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;

        if(metrics.total_bytes > 0){
            uint64_t used_bytes = metrics.total_bytes - metrics.free_bytes;
            metrics.used_percentage = ( static_cast<double> (used_bytes) / metrics.total_bytes) * 100.0;
        }

        if(metrics.used_percentage >= 90.0 ){
            metrics.status = HealthStatus::CRITICAL;
        }else if(metrics.used_percentage >= 75.0){
            metrics.status = HealthStatus::WARNING;
        }else{
            metrics.status = HealthStatus::HEALTHY;
        }

        return metrics;

    }

    std::string StorageCollector::StatusToString(HealthStatus status){
        switch(status){
            case HealthStatus::HEALTHY: return "HEALTHY";
            case HealthStatus::WARNING: return "WARNING";
            case HealthStatus::CRITICAL: return "CRITICAL";
            default: return "UNKNOWN";
        }
    }

    std::string StorageCollector::GenerateReportJson(const std::string& path){
        PartitionMetrics m = Collect(path);
        std::ostringstream ss;
        ss << "{"
            << "\"mount\":\"" << m.mount_point << "\","
            << "\"total_mb\":" << (m.total_bytes / (1024 * 1024)) << ","
            << "\"available_mb\":" << (m.available_bytes / (1024 * 1024)) << ","
            << "\"used_pct\":" << std::fixed << std::setprecision(2) << m.used_percentage << ","
            << "\"status\":\"" << StatusToString(m.status) << "\""
            << "}\n";

        
        return ss.str();
    }

}