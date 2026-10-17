#pragma once

#include <string>
#include <cstdint>

namespace android::storage::guard {

    enum class HealthStatus {
        HEALTHY,
        WARNING,
        CRITICAL
    };

    struct PartitionMetrics {
        std::string mount_point;
        uint64_t total_bytes;
        uint64_t free_bytes;
        uint64_t available_bytes;
        double used_percentage;
        HealthStatus status;
    };

    class StorageCollector {
    public:
        static PartitionMetrics Collect(const std::string& path);
        static std::string StatusToString(HealthStatus status);
        static std::string GenerateReportJson(const std::string& path);

    };



}