/**
 * MR Deregistration Abuse Attack Test
 * Based on Husky framework and Understanding RDMA findings
 * 
 * This test simulates the MR deregistration abuse attack described in
 * "Understanding RDMA Microarchitecture Resources for Performance Isolation" (NSDI'23)
 * 
 * Attack pattern: Rapidly register and deregister MRs to cause MTT cache thrashing
 * Expected effect: Degrade performance for other tenants sharing the same NIC
 */

#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/time.h>
#include <infiniband/verbs.h>

#define TEST_DURATION 30  // seconds
#define MR_SIZE (4 * 1024 * 1024)  // 4MB per MR
#define NUM_MRS 100  // Number of MRs to rotate through

struct TestConfig {
    const char* dev_name = "mlx5_0";
    int duration_sec = TEST_DURATION;
    int num_mrs = NUM_MRS;
    size_t mr_size = MR_SIZE;
    int batch_size = 10;  // Register/deregister in batches
};

class MRDeregAbuseTest {
private:
    struct ibv_device* device_;
    struct ibv_context* ctx_;
    struct ibv_pd* pd_;
    std::vector<void*> buffers_;
    std::vector<struct ibv_mr*> mrs_;
    TestConfig config_;
    
    uint64_t GetTimeUs() {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return tv.tv_sec * 1000000 + tv.tv_usec;
    }
    
public:
    MRDeregAbuseTest(const TestConfig& config) : config_(config) {
        device_ = nullptr;
        ctx_ = nullptr;
        pd_ = nullptr;
    }
    
    ~MRDeregAbuseTest() {
        Cleanup();
    }
    
    bool Init() {
        // Get device list
        int num_devices;
        struct ibv_device** dev_list = ibv_get_device_list(&num_devices);
        if (!dev_list || num_devices == 0) {
            std::cerr << "No IB devices found" << std::endl;
            return false;
        }
        
        // Find specified device
        for (int i = 0; i < num_devices; i++) {
            if (strcmp(dev_list[i]->name, config_.dev_name) == 0) {
                device_ = dev_list[i];
                break;
            }
        }
        
        if (!device_) {
            std::cerr << "Device " << config_.dev_name << " not found" << std::endl;
            ibv_free_device_list(dev_list);
            return false;
        }
        
        // Open device
        ctx_ = ibv_open_device(device_);
        if (!ctx_) {
            std::cerr << "Failed to open device" << std::endl;
            ibv_free_device_list(dev_list);
            return false;
        }
        
        // Allocate protection domain
        pd_ = ibv_alloc_pd(ctx_);
        if (!pd_) {
            std::cerr << "Failed to allocate PD" << std::endl;
            ibv_free_device_list(dev_list);
            return false;
        }
        
        // Pre-allocate buffers
        buffers_.resize(config_.num_mrs);
        for (int i = 0; i < config_.num_mrs; i++) {
            buffers_[i] = aligned_alloc(4096, config_.mr_size);
            if (!buffers_[i]) {
                std::cerr << "Failed to allocate buffer " << i << std::endl;
                return false;
            }
            memset(buffers_[i], 0, config_.mr_size);
        }
        
        mrs_.resize(config_.num_mrs, nullptr);
        
        ibv_free_device_list(dev_list);
        std::cout << "[Init] Device: " << config_.dev_name 
                  << ", MRs: " << config_.num_mrs 
                  << ", MR size: " << (config_.mr_size / (1024*1024)) << "MB"
                  << std::endl;
        return true;
    }
    
    void Cleanup() {
        // Deregister all MRs
        for (auto mr : mrs_) {
            if (mr) {
                ibv_dereg_mr(mr);
            }
        }
        mrs_.clear();
        
        // Free buffers
        for (auto buf : buffers_) {
            if (buf) {
                free(buf);
            }
        }
        buffers_.clear();
        
        if (pd_) {
            ibv_dealloc_pd(pd_);
            pd_ = nullptr;
        }
        
        if (ctx_) {
            ibv_close_device(ctx_);
            ctx_ = nullptr;
        }
    }
    
    // Phase 1: Initial allocation
    void AllocPhase() {
        std::cout << "[Phase 1] Initial allocation of " << config_.num_mrs << " MRs" << std::endl;
        
        for (int i = 0; i < config_.num_mrs; i++) {
            mrs_[i] = ibv_reg_mr(pd_, buffers_[i], config_.mr_size,
                                 IBV_ACCESS_LOCAL_WRITE | 
                                 IBV_ACCESS_REMOTE_READ | 
                                 IBV_ACCESS_REMOTE_WRITE);
            if (!mrs_[i]) {
                std::cerr << "Failed to register MR " << i << std::endl;
                return;
            }
        }
        
        std::cout << "[Phase 1] Completed: " << config_.num_mrs << " MRs registered" << std::endl;
    }
    
    // Phase 2: Abuse - Rapid deregister and reregister
    void AbusePhase() {
        std::cout << "[Phase 2] MR Deregistration Abuse Attack for " 
                  << config_.duration_sec << " seconds" << std::endl;
        std::cout << "[Phase 2] Pattern: Deregister " << config_.batch_size 
                  << " MRs, then reregister them (repeated)" << std::endl;
        
        uint64_t start_time = GetTimeUs();
        uint64_t end_time = start_time + config_.duration_sec * 1000000;
        
        uint64_t total_ops = 0;
        uint64_t dereg_lat_total = 0;
        uint64_t reg_lat_total = 0;
        int batch_idx = 0;
        
        while (GetTimeUs() < end_time) {
            // Select batch of MRs to abuse
            int start_idx = (batch_idx * config_.batch_size) % config_.num_mrs;
            
            // Deregister batch
            for (int i = 0; i < config_.batch_size; i++) {
                int idx = (start_idx + i) % config_.num_mrs;
                if (mrs_[idx]) {
                    uint64_t dereg_start = GetTimeUs();
                    int ret = ibv_dereg_mr(mrs_[idx]);
                    uint64_t dereg_end = GetTimeUs();
                    
                    if (ret != 0) {
                        std::cerr << "Deregister MR " << idx << " failed" << std::endl;
                        continue;
                    }
                    
                    mrs_[idx] = nullptr;
                    dereg_lat_total += (dereg_end - dereg_start);
                }
            }
            
            // Reregister batch (immediately)
            for (int i = 0; i < config_.batch_size; i++) {
                int idx = (start_idx + i) % config_.num_mrs;
                if (!mrs_[idx]) {
                    uint64_t reg_start = GetTimeUs();
                    mrs_[idx] = ibv_reg_mr(pd_, buffers_[idx], config_.mr_size,
                                           IBV_ACCESS_LOCAL_WRITE | 
                                           IBV_ACCESS_REMOTE_READ | 
                                           IBV_ACCESS_REMOTE_WRITE);
                    uint64_t reg_end = GetTimeUs();
                    
                    if (!mrs_[idx]) {
                        std::cerr << "Reregister MR " << idx << " failed" << std::endl;
                        continue;
                    }
                    
                    reg_lat_total += (reg_end - reg_start);
                    total_ops++;
                }
            }
            
            batch_idx++;
            
            // Progress report every 5 seconds
            uint64_t elapsed = (GetTimeUs() - start_time) / 1000000;
            static uint64_t last_report = 0;
            if (elapsed - last_report >= 5) {
                std::cout << "[Progress] Time: " << elapsed << "s, "
                          << "Cycles: " << batch_idx << ", "
                          << "Total MR ops: " << total_ops << std::endl;
                last_report = elapsed;
            }
        }
        
        // Print statistics
        std::cout << "\n[Phase 2] Attack Statistics:" << std::endl;
        std::cout << "  Total deregister+register cycles: " << batch_idx << std::endl;
        std::cout << "  Total MR operations: " << total_ops << std::endl;
        if (total_ops > 0) {
            std::cout << "  Avg deregister latency: " << (dereg_lat_total / total_ops) << " us" << std::endl;
            std::cout << "  Avg register latency: " << (reg_lat_total / total_ops) << " us" << std::endl;
        }
    }
    
    // Phase 3: Cleanup
    void DeallocPhase() {
        std::cout << "[Phase 3] Cleanup - Deregister all MRs" << std::endl;
        
        int count = 0;
        for (int i = 0; i < config_.num_mrs; i++) {
            if (mrs_[i]) {
                ibv_dereg_mr(mrs_[i]);
                mrs_[i] = nullptr;
                count++;
            }
        }
        
        std::cout << "[Phase 3] Completed: " << count << " MRs deregistered" << std::endl;
    }
    
    void Run() {
        if (!Init()) {
            return;
        }
        
        AllocPhase();
        AbusePhase();
        DeallocPhase();
        
        std::cout << "\n[Test Complete] MR Deregistration Abuse Attack finished" << std::endl;
    }
};

void PrintUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -d, --device <dev>    RDMA device name (default: mlx5_0)\n"
              << "  -t, --time <sec>      Test duration in seconds (default: 30)\n"
              << "  -n, --num-mrs <num>   Number of MRs to rotate (default: 100)\n"
              << "  -s, --size <bytes>    MR size in bytes (default: 4MB)\n"
              << "  -b, --batch <num>     Batch size for deregister/reregister (default: 10)\n"
              << "  -h, --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    TestConfig config;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--device") == 0) {
            if (i + 1 < argc) config.dev_name = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--time") == 0) {
            if (i + 1 < argc) config.duration_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--num-mrs") == 0) {
            if (i + 1 < argc) config.num_mrs = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0) {
            if (i + 1 < argc) config.mr_size = atol(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--batch") == 0) {
            if (i + 1 < argc) config.batch_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            return 0;
        }
    }
    
    std::cout << "========================================\n";
    std::cout << "MR Deregistration Abuse Attack Test\n";
    std::cout << "Based on Understanding RDMA (NSDI'23)\n";
    std::cout << "========================================\n";
    std::cout << "Configuration:\n";
    std::cout << "  Device: " << config.dev_name << "\n";
    std::cout << "  Duration: " << config.duration_sec << " seconds\n";
    std::cout << "  Num MRs: " << config.num_mrs << "\n";
    std::cout << "  MR Size: " << (config.mr_size / (1024*1024)) << " MB\n";
    std::cout << "  Batch Size: " << config.batch_size << "\n";
    std::cout << "========================================\n\n";
    
    MRDeregAbuseTest test(config);
    test.Run();
    
    return 0;
}
