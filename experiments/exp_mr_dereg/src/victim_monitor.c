/**
 * Victim Bandwidth Monitor
 * Monitor Victim bandwidth under attack
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <stdint.h>

#define DEFAULT_DURATION 30

static volatile int keep_running = 1;

void signal_handler(int sig) {
    keep_running = 0;
}

// Get current time (microseconds)
static inline uint64_t get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <duration_sec> <output_csv> [server_ip]\n", argv[0]);
        fprintf(stderr, "  If server_ip is provided, run as client; otherwise as server.\n");
        return 1;
    }
    
    int duration = atoi(argv[1]);
    const char* output_file = argv[2];
    const char* server_ip = (argc > 3) ? argv[3] : NULL;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("[Victim] Starting bandwidth monitor for %d seconds...\n", duration);
    printf("[Victim] Output: %s\n", output_file);
    
    // Open output file
    FILE* fp = fopen(output_file, "w");
    if (!fp) {
        perror("Failed to open output file");
        return 1;
    }
    fprintf(fp, "TimeSec,BandwidthMbps\n");
    
    // Use ib_write_bw as Victim workload
    char cmd[512];
    if (server_ip) {
        // Client mode
        snprintf(cmd, sizeof(cmd), 
                 "ib_write_bw -d mlx5_0 -x 2 -s 1048576 -q 8 -D %d --report_gbits %s 2>&1",
                 duration + 5, server_ip);
    } else {
        // Server mode
        snprintf(cmd, sizeof(cmd), 
                 "ib_write_bw -d mlx5_0 -x 2 -s 1048576 -q 8 -D %d --report_gbits 2>&1",
                 duration + 5);
    }
    
    printf("[Victim] Command: %s\n", cmd);
    
    // Start ib_write_bw and parse output
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        perror("Failed to run ib_write_bw");
        fclose(fp);
        return 1;
    }
    
    char line[256];
    uint64_t start_time = get_time_us();
    
    while (fgets(line, sizeof(line), pipe) != NULL && keep_running) {
        // Parse bandwidth data from ib_write_bw output
        // Format example: "  1000        1000      1000.00      1000.00"
        double bw_gbps = 0;
        
        // Try to parse standard output format
        int iter;
        double bw, lat;
        if (sscanf(line, "%d %*d %lf %lf", &iter, &bw, &lat) == 3) {
            bw_gbps = bw;
        }
        
        if (bw_gbps > 0 && bw_gbps < 1000) {  // Sanity check
            uint64_t elapsed_us = get_time_us() - start_time;
            double sec = elapsed_us / 1000000.0;
            
            fprintf(fp, "%.2f,%.2f\n", sec, bw_gbps * 1000);  // Convert to Mbps
            fflush(fp);
            printf("[Victim] t=%.1fs, BW=%.2f Gbps\n", sec, bw_gbps);
        }
        
        // Check timeout
        if ((get_time_us() - start_time) > (uint64_t)(duration + 2) * 1000000) {
            break;
        }
    }
    
    pclose(pipe);
    fclose(fp);
    
    printf("[Victim] Monitor complete. Results saved to: %s\n", output_file);
    return 0;
}
