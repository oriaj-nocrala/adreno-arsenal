#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <pthread.h>

#define KGSL_IOC_TYPE 0x09
#define IOCTL_KGSL_DEVICE_GETPROPERTY    _IOWR(KGSL_IOC_TYPE, 0x2, struct kgsl_device_getproperty)
#define IOCTL_KGSL_GPUMEM_ALLOC_ID       _IOWR(KGSL_IOC_TYPE, 0x34, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPUMEM_FREE_ID        _IOWR(KGSL_IOC_TYPE, 0x35, struct kgsl_gpumem_free_id)

struct kgsl_device_getproperty {
    unsigned int type;
    void *value;
    unsigned int sizebytes;
};

struct kgsl_devinfo {
    unsigned int device_id;
    unsigned int chip_id;
    unsigned int mmu_enabled;
    unsigned int gmem_gpubaseaddr;
    unsigned int gpu_id;
    unsigned int gmem_sizebytes;
};

struct kgsl_gpumem_alloc_id {
    unsigned int id;
    unsigned int flags;
    unsigned int size;
    unsigned int mmapsize;
    unsigned long long gpuaddr;
};

struct kgsl_gpumem_free_id {
    unsigned int id;
};

// ===============================================
// GPU MONITOR PRO - ADVANCED SYSTEM
// ===============================================

typedef struct {
    float temp_celsius;
    int freq_mhz;
    int util_percent;
    int power_level;
    unsigned long memory_used;
    unsigned long memory_total;
    int throttle_status;
    float voltage;
    int reset_count;
    time_t timestamp;
} gpu_metrics_t;

typedef struct {
    int gpu_fd;
    pthread_t monitor_thread;
    volatile int running;
    gpu_metrics_t current_metrics;
    gpu_metrics_t history[3600];  // 1 hour at 1 sample/sec
    int history_index;
    FILE *log_file;
    int alert_threshold_temp;
    int alert_threshold_util;
} gpu_monitor_t;

// Colors for terminal output
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BOLD    "\033[1m"
#define RESET   "\033[0m"

volatile int keep_running = 1;

void signal_handler(int sig) {
    keep_running = 0;
}

// Read GPU temperature with fallback methods
float read_gpu_temperature() {
    FILE *f;
    char buffer[128];
    float temp = 0.0f;
    
    // Method 1: Direct KGSL temperature
    f = fopen("/sys/class/kgsl/kgsl-3d0/temp", "r");
    if (f) {
        if (fgets(buffer, sizeof(buffer), f)) {
            temp = atoi(buffer) / 1000.0f;  // Convert from millidegrees
        }
        fclose(f);
        if (temp > 0) return temp;
    }
    
    // Method 2: Thermal zone (backup)
    for (int i = 0; i < 20; i++) {
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type", i);
        f = fopen(path, "r");
        if (f) {
            if (fgets(buffer, sizeof(buffer), f)) {
                if (strstr(buffer, "gpu") || strstr(buffer, "GPU")) {
                    fclose(f);
                    snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
                    f = fopen(path, "r");
                    if (f) {
                        if (fgets(buffer, sizeof(buffer), f)) {
                            temp = atoi(buffer) / 1000.0f;
                        }
                        fclose(f);
                        if (temp > 0) return temp;
                    }
                }
            } else {
                fclose(f);
            }
        }
    }
    
    return 0.0f;  // Unknown
}

// Read GPU frequency
int read_gpu_frequency() {
    FILE *f = fopen("/sys/class/kgsl/kgsl-3d0/clock_mhz", "r");
    if (f) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), f)) {
            fclose(f);
            return atoi(buffer);
        }
        fclose(f);
    }
    return 0;
}

// Read GPU utilization
int read_gpu_utilization() {
    FILE *f = fopen("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage", "r");
    if (f) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), f)) {
            fclose(f);
            return atoi(buffer);
        }
        fclose(f);
    }
    return 0;
}

// Read power level
int read_power_level() {
    FILE *f = fopen("/sys/class/kgsl/kgsl-3d0/default_pwrlevel", "r");
    if (f) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), f)) {
            fclose(f);
            return atoi(buffer);
        }
        fclose(f);
    }
    return -1;
}

// Read thermal power level (throttling indicator)
int read_thermal_status() {
    FILE *f = fopen("/sys/class/kgsl/kgsl-3d0/thermal_pwrlevel", "r");
    if (f) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), f)) {
            fclose(f);
            return atoi(buffer);
        }
        fclose(f);
    }
    return 0;
}

// Read GPU reset count
int read_reset_count() {
    FILE *f = fopen("/sys/class/kgsl/kgsl-3d0/reset_count", "r");
    if (f) {
        char buffer[64];
        if (fgets(buffer, sizeof(buffer), f)) {
            fclose(f);
            return atoi(buffer);
        }
        fclose(f);
    }
    return 0;
}

// Estimate GPU voltage based on frequency and power level
float estimate_gpu_voltage(int freq_mhz, int power_level) {
    // Rough estimation based on typical Adreno 730 characteristics
    float base_voltage = 0.8f;  // 800mV base
    float freq_factor = (freq_mhz / 818.0f) * 0.3f;  // Scale with frequency
    float power_factor = power_level * 0.05f;  // Power level adjustment
    
    return base_voltage + freq_factor + power_factor;
}

// Memory usage estimation
unsigned long estimate_gpu_memory_usage(int gpu_fd) {
    // Try to allocate test buffers to estimate available memory
    struct kgsl_gpumem_alloc_id test_buffer;
    unsigned long total_allocated = 0;
    int allocation_count = 0;
    
    // Try multiple small allocations
    for (int i = 0; i < 10; i++) {
        memset(&test_buffer, 0, sizeof(test_buffer));
        test_buffer.size = 1048576;  // 1MB each
        test_buffer.flags = 0;
        
        if (ioctl(gpu_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &test_buffer) == 0) {
            total_allocated += test_buffer.size;
            allocation_count++;
            
            struct kgsl_gpumem_free_id free_req = { .id = test_buffer.id };
            ioctl(gpu_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &free_req);
        } else {
            break;
        }
    }
    
    return total_allocated;
}

// Collect all GPU metrics
void collect_metrics(gpu_monitor_t *monitor) {
    gpu_metrics_t *metrics = &monitor->current_metrics;
    
    metrics->temp_celsius = read_gpu_temperature();
    metrics->freq_mhz = read_gpu_frequency();
    metrics->util_percent = read_gpu_utilization();
    metrics->power_level = read_power_level();
    metrics->throttle_status = read_thermal_status();
    metrics->reset_count = read_reset_count();
    metrics->voltage = estimate_gpu_voltage(metrics->freq_mhz, metrics->power_level);
    metrics->memory_used = estimate_gpu_memory_usage(monitor->gpu_fd);
    metrics->memory_total = 1024 * 1024 * 1024;  // Assume 1GB GPU memory pool
    metrics->timestamp = time(NULL);
    
    // Store in history
    monitor->history[monitor->history_index] = *metrics;
    monitor->history_index = (monitor->history_index + 1) % 3600;
}

// Display temperature with color coding
void display_temperature(float temp) {
    const char *color;
    const char *status;
    
    if (temp < 40) {
        color = CYAN;
        status = "COOL";
    } else if (temp < 60) {
        color = GREEN;
        status = "NORMAL";
    } else if (temp < 75) {
        color = YELLOW;
        status = "WARM";
    } else if (temp < 85) {
        color = RED;
        status = "HOT";
    } else {
        color = BOLD RED;
        status = "CRITICAL";
    }
    
    printf("%s%5.1f°C [%s]%s", color, temp, status, RESET);
}

// Display frequency with performance indication
void display_frequency(int freq_mhz) {
    const char *color;
    float performance = (freq_mhz / 818.0f) * 100;  // 818 MHz = 100%
    
    if (performance > 90) {
        color = RED;
    } else if (performance > 70) {
        color = YELLOW;
    } else if (performance > 40) {
        color = GREEN;
    } else {
        color = CYAN;
    }
    
    printf("%s%4d MHz [%3.0f%%]%s", color, freq_mhz, performance, RESET);
}

// Display utilization bar
void display_utilization_bar(int util_percent) {
    const char *color;
    
    if (util_percent > 85) {
        color = RED;
    } else if (util_percent > 60) {
        color = YELLOW;
    } else if (util_percent > 30) {
        color = GREEN;
    } else {
        color = CYAN;
    }
    
    printf("%s%3d%% [", color, util_percent);
    
    int bars = util_percent / 5;  // 20 bars max
    for (int i = 0; i < 20; i++) {
        if (i < bars) {
            printf("█");
        } else {
            printf("░");
        }
    }
    printf("]%s", RESET);
}

// Display memory usage
void display_memory_usage(unsigned long used, unsigned long total) {
    float usage_percent = (total > 0) ? (used * 100.0f / total) : 0;
    const char *color;
    
    if (usage_percent > 85) {
        color = RED;
    } else if (usage_percent > 60) {
        color = YELLOW;
    } else {
        color = GREEN;
    }
    
    printf("%s%lu MB / %lu MB [%3.1f%%]%s", 
           color, used / (1024*1024), total / (1024*1024), usage_percent, RESET);
}

// Real-time monitoring display
void display_realtime_monitor(gpu_monitor_t *monitor) {
    gpu_metrics_t *m = &monitor->current_metrics;
    
    // Clear screen and move cursor to top
    printf("\033[2J\033[H");
    
    // Header
    printf("%s%s════════════════════════════════════════════════════════════════════════════════%s\n", BOLD, CYAN, RESET);
    printf("%s%s                           🚀 GPU MONITOR PRO 🚀                               %s\n", BOLD, CYAN, RESET);
    printf("%s%s                      Samsung Galaxy S22 Adreno 730v2                        %s\n", BOLD, CYAN, RESET);
    printf("%s%s════════════════════════════════════════════════════════════════════════════════%s\n", BOLD, CYAN, RESET);
    
    // Main metrics display
    printf("\n");
    printf("🌡️  Temperature: ");
    display_temperature(m->temp_celsius);
    printf("         ⚡ Frequency: ");
    display_frequency(m->freq_mhz);
    printf("\n\n");
    
    printf("📊 Utilization: ");
    display_utilization_bar(m->util_percent);
    printf("\n\n");
    
    printf("💾 Memory:      ");
    display_memory_usage(m->memory_used, m->memory_total);
    printf("\n\n");
    
    // Additional metrics
    printf("⚙️  Power Level: %s%d%s", YELLOW, m->power_level, RESET);
    printf("         🔋 Voltage: %s%.2fV%s\n", MAGENTA, m->voltage, RESET);
    
    printf("🛡️  Throttling:  %s%s%s", 
           m->throttle_status > 0 ? RED "ACTIVE" : GREEN "NONE", 
           m->throttle_status > 0 ? RED : GREEN, RESET);
    printf("         🔄 Resets: %s%d%s\n", WHITE, m->reset_count, RESET);
    
    // Performance analysis
    printf("\n%s%s── PERFORMANCE ANALYSIS ──%s\n", BOLD, YELLOW, RESET);
    
    float perf_score = (m->freq_mhz / 818.0f) * (1.0f - m->throttle_status * 0.3f) * 
                      (m->temp_celsius < 75 ? 1.0f : 0.7f);
    
    printf("Performance Score: %s%.1f/10%s", 
           perf_score > 8 ? GREEN : (perf_score > 6 ? YELLOW : RED), 
           perf_score * 10, RESET);
    
    if (m->temp_celsius > 80) {
        printf("   %s⚠️ THERMAL WARNING%s", RED, RESET);
    }
    if (m->throttle_status > 0) {
        printf("   %s🐌 THROTTLED%s", RED, RESET);
    }
    if (m->util_percent > 95) {
        printf("   %s🔥 MAX LOAD%s", YELLOW, RESET);
    }
    
    printf("\n");
    
    // Historical graphs (mini)
    printf("\n%s%s── FREQUENCY HISTORY (Last 60s) ──%s\n", BOLD, BLUE, RESET);
    printf("MHz: ");
    for (int i = 0; i < 60; i++) {
        int idx = (monitor->history_index - 60 + i + 3600) % 3600;
        int freq = monitor->history[idx].freq_mhz;
        if (freq > 700) printf("█");
        else if (freq > 500) printf("▓");
        else if (freq > 300) printf("▒");
        else if (freq > 0) printf("░");
        else printf(" ");
    }
    printf("\n");
    
    // Footer with controls
    printf("\n%s%s────────────────────────────────────────────────────────────────────────────────%s\n", BOLD, CYAN, RESET);
    printf("Last Update: %s", ctime(&m->timestamp));
    printf("Press Ctrl+C to stop monitoring | Logging to: gpu_monitor.log\n");
    printf("%s%s════════════════════════════════════════════════════════════════════════════════%s\n", BOLD, CYAN, RESET);
}

// Log metrics to file
void log_metrics(gpu_monitor_t *monitor) {
    gpu_metrics_t *m = &monitor->current_metrics;
    
    if (monitor->log_file) {
        fprintf(monitor->log_file, "%ld,%.1f,%d,%d,%d,%lu,%.2f,%d,%d\n",
                m->timestamp, m->temp_celsius, m->freq_mhz, m->util_percent,
                m->power_level, m->memory_used, m->voltage, m->throttle_status,
                m->reset_count);
        fflush(monitor->log_file);
    }
}

// Check for alerts
void check_alerts(gpu_monitor_t *monitor) {
    gpu_metrics_t *m = &monitor->current_metrics;
    
    if (m->temp_celsius > monitor->alert_threshold_temp) {
        printf("\n%s🚨 TEMPERATURE ALERT: %.1f°C > %d°C threshold!%s\n", 
               RED, m->temp_celsius, monitor->alert_threshold_temp, RESET);
    }
    
    if (m->util_percent > monitor->alert_threshold_util) {
        printf("\n%s🚨 UTILIZATION ALERT: %d%% > %d%% threshold!%s\n", 
               RED, m->util_percent, monitor->alert_threshold_util, RESET);
    }
    
    if (m->throttle_status > 0) {
        printf("\n%s🚨 THROTTLING ALERT: GPU performance reduced!%s\n", RED, RESET);
    }
}

// Main monitoring thread
void* monitor_thread_func(void *arg) {
    gpu_monitor_t *monitor = (gpu_monitor_t*)arg;
    
    while (monitor->running && keep_running) {
        collect_metrics(monitor);
        display_realtime_monitor(monitor);
        log_metrics(monitor);
        check_alerts(monitor);
        
        sleep(1);
    }
    
    return NULL;
}

// Initialize GPU monitor
int init_gpu_monitor(gpu_monitor_t *monitor) {
    printf("🚀 Initializing GPU Monitor Pro...\n");
    
    monitor->gpu_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (monitor->gpu_fd < 0) {
        printf("❌ Error opening GPU device: %s\n", strerror(errno));
        return -1;
    }
    
    monitor->running = 1;
    monitor->history_index = 0;
    monitor->alert_threshold_temp = 85;  // 85°C
    monitor->alert_threshold_util = 95;  // 95%
    
    // Open log file
    monitor->log_file = fopen("gpu_monitor.log", "a");
    if (monitor->log_file) {
        fprintf(monitor->log_file, "# GPU Monitor Pro Log - Started at %ld\n", time(NULL));
        fprintf(monitor->log_file, "timestamp,temp_c,freq_mhz,util_percent,power_level,memory_used,voltage,throttle,resets\n");
    }
    
    printf("✅ GPU Monitor Pro initialized successfully!\n");
    printf("📝 Logging to: gpu_monitor.log\n");
    printf("🎯 Temperature alert threshold: %d°C\n", monitor->alert_threshold_temp);
    printf("🎯 Utilization alert threshold: %d%%\n", monitor->alert_threshold_util);
    printf("\nStarting real-time monitoring in 3 seconds...\n");
    
    sleep(3);
    
    return 0;
}

void cleanup_gpu_monitor(gpu_monitor_t *monitor) {
    monitor->running = 0;
    
    if (monitor->gpu_fd >= 0) {
        close(monitor->gpu_fd);
    }
    
    if (monitor->log_file) {
        fprintf(monitor->log_file, "# GPU Monitor Pro stopped at %ld\n", time(NULL));
        fclose(monitor->log_file);
    }
    
    printf("\n🛑 GPU Monitor Pro stopped. Log saved to gpu_monitor.log\n");
}

int main(int argc, char *argv[]) {
    gpu_monitor_t monitor;
    
    printf("%s%s🚀 GPU MONITOR PRO 🚀%s\n", BOLD, CYAN, RESET);
    printf("Advanced GPU monitoring for Samsung Galaxy S22 Adreno 730v2\n\n");
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    
    if (init_gpu_monitor(&monitor) != 0) {
        return 1;
    }
    
    // Start monitoring thread
    if (pthread_create(&monitor.monitor_thread, NULL, monitor_thread_func, &monitor) != 0) {
        printf("❌ Failed to create monitoring thread\n");
        cleanup_gpu_monitor(&monitor);
        return 1;
    }
    
    // Wait for monitoring thread to complete
    pthread_join(monitor.monitor_thread, NULL);
    
    cleanup_gpu_monitor(&monitor);
    
    printf("Thanks for using GPU Monitor Pro! 🎉\n");
    return 0;
}