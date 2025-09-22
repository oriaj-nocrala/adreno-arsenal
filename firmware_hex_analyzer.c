#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>
#include <math.h>

// ===============================================
// ADRENO FIRMWARE HEX ANALYZER
// ===============================================

typedef struct {
    char magic[8];
    char description[64];
    int color_code;
} firmware_signature_t;

typedef struct {
    unsigned long offset;
    unsigned char *data;
    size_t size;
    char description[128];
    int type;
} firmware_section_t;

typedef struct {
    FILE *file;
    char *filename;
    unsigned char *buffer;
    size_t file_size;
    firmware_section_t sections[32];
    int section_count;
} firmware_analyzer_t;

// Colors for output
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BOLD    "\033[1m"
#define RESET   "\033[0m"

// Known firmware signatures
firmware_signature_t known_signatures[] = {
    {"ELF", "ELF Binary", 32},
    {"\x7F" "ELF", "ELF Executable", 32},
    {"MZ", "PE Executable", 33},
    {"ADRENO", "Adreno Firmware", 35},
    {"QCOM", "Qualcomm Binary", 36},
    {"ARM", "ARM Binary", 34},
    {"\x00\x00\x00\x00", "Zero Block", 37},
    {"\xFF\xFF\xFF\xFF", "Padding Block", 37},
    {"KGSL", "KGSL Driver Data", 35},
    {"GPU", "GPU Specific Data", 34},
    {"\xDE\xAD\xBE\xEF", "Debug Marker", 31},
    {"\xCA\xFE\xBA\xBE", "Magic Number", 31},
};

// Hex dump with advanced analysis
void print_hex_dump_advanced(unsigned char *data, size_t size, unsigned long base_offset) {
    for (size_t i = 0; i < size; i += 16) {
        // Address
        printf(CYAN "%08lx: " RESET, base_offset + i);
        
        // Hex bytes with color coding
        for (int j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char byte = data[i + j];
                
                // Color code based on byte value
                if (byte == 0x00) {
                    printf(WHITE "%02x " RESET, byte);
                } else if (byte == 0xFF) {
                    printf(RED "%02x " RESET, byte);
                } else if (isprint(byte)) {
                    printf(GREEN "%02x " RESET, byte);
                } else if (byte > 0x80) {
                    printf(YELLOW "%02x " RESET, byte);
                } else {
                    printf("%02x ", byte);
                }
            } else {
                printf("   ");
            }
            
            if (j == 7) printf(" ");
        }
        
        printf(" ");
        
        // ASCII representation
        for (int j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char byte = data[i + j];
                if (isprint(byte)) {
                    printf(GREEN "%c" RESET, byte);
                } else {
                    printf(WHITE "." RESET);
                }
            }
        }
        
        printf("\n");
    }
}

// Detect patterns and structures
void analyze_patterns(firmware_analyzer_t *analyzer) {
    printf("\n" BOLD CYAN "🔍 PATTERN ANALYSIS 🔍\n" RESET);
    printf("═══════════════════════════════════════\n");
    
    unsigned char *data = analyzer->buffer;
    size_t size = analyzer->file_size;
    
    // Look for repeating patterns
    printf("Searching for repeating patterns...\n");
    
    int pattern_counts[256] = {0};
    for (size_t i = 0; i < size; i++) {
        pattern_counts[data[i]]++;
    }
    
    printf("\nByte frequency analysis:\n");
    printf("Most common bytes:\n");
    
    // Find top 5 most common bytes
    for (int rank = 0; rank < 5; rank++) {
        int max_count = 0;
        int max_byte = 0;
        
        for (int b = 0; b < 256; b++) {
            if (pattern_counts[b] > max_count) {
                max_count = pattern_counts[b];
                max_byte = b;
            }
        }
        
        if (max_count > 0) {
            float percentage = (max_count * 100.0f) / size;
            printf("  0x%02x: %d times (%.1f%%) ", max_byte, max_count, percentage);
            
            if (max_byte == 0x00) {
                printf(WHITE "- NULL bytes" RESET);
            } else if (max_byte == 0xFF) {
                printf(RED "- Padding/Uninitialized" RESET);
            } else if (isprint(max_byte)) {
                printf(GREEN "- ASCII '%c'" RESET, max_byte);
            }
            printf("\n");
            
            pattern_counts[max_byte] = 0;  // Remove for next iteration
        }
    }
    
    // Look for sequential patterns
    printf("\nSequential pattern analysis:\n");
    int consecutive_zeros = 0;
    int max_consecutive_zeros = 0;
    int consecutive_ff = 0;
    int max_consecutive_ff = 0;
    
    for (size_t i = 0; i < size; i++) {
        if (data[i] == 0x00) {
            consecutive_zeros++;
            consecutive_ff = 0;
        } else if (data[i] == 0xFF) {
            consecutive_ff++;
            consecutive_zeros = 0;
        } else {
            if (consecutive_zeros > max_consecutive_zeros) {
                max_consecutive_zeros = consecutive_zeros;
            }
            if (consecutive_ff > max_consecutive_ff) {
                max_consecutive_ff = consecutive_ff;
            }
            consecutive_zeros = 0;
            consecutive_ff = 0;
        }
    }
    
    printf("  Max consecutive zeros: %d bytes", max_consecutive_zeros);
    if (max_consecutive_zeros > 64) {
        printf(YELLOW " - Likely padding or uninitialized data" RESET);
    }
    printf("\n");
    
    printf("  Max consecutive 0xFF: %d bytes", max_consecutive_ff);
    if (max_consecutive_ff > 64) {
        printf(RED " - Likely flash memory padding" RESET);
    }
    printf("\n");
}

// Search for known signatures
void search_signatures(firmware_analyzer_t *analyzer) {
    printf("\n" BOLD YELLOW "🔍 SIGNATURE SCANNING 🔍\n" RESET);
    printf("═══════════════════════════════════════\n");
    
    unsigned char *data = analyzer->buffer;
    size_t size = analyzer->file_size;
    int signatures_found = 0;
    
    for (int sig_idx = 0; sig_idx < sizeof(known_signatures)/sizeof(known_signatures[0]); sig_idx++) {
        firmware_signature_t *sig = &known_signatures[sig_idx];
        int sig_len = strlen(sig->magic);
        
        for (size_t i = 0; i <= size - sig_len; i++) {
            if (memcmp(data + i, sig->magic, sig_len) == 0) {
                printf("✓ Found signature at 0x%08lx: ", i);
                printf("\033[%dm%s\033[0m", sig->color_code, sig->description);
                printf(" (");
                
                for (int j = 0; j < sig_len; j++) {
                    if (isprint(sig->magic[j])) {
                        printf("%c", sig->magic[j]);
                    } else {
                        printf("\\x%02x", (unsigned char)sig->magic[j]);
                    }
                }
                printf(")\n");
                
                signatures_found++;
                
                // Show context around signature
                printf("  Context: ");
                size_t start = (i >= 8) ? i - 8 : 0;
                size_t end = (i + sig_len + 8 < size) ? i + sig_len + 8 : size;
                
                for (size_t j = start; j < end; j++) {
                    if (j >= i && j < i + sig_len) {
                        printf(BOLD "%02x " RESET, data[j]);
                    } else {
                        printf("%02x ", data[j]);
                    }
                }
                printf("\n");
            }
        }
    }
    
    if (signatures_found == 0) {
        printf("No known signatures found.\n");
    } else {
        printf("\nTotal signatures found: %d\n", signatures_found);
    }
}

// Entropy analysis
void analyze_entropy(firmware_analyzer_t *analyzer) {
    printf("\n" BOLD MAGENTA "📊 ENTROPY ANALYSIS 📊\n" RESET);
    printf("═══════════════════════════════════════\n");
    
    unsigned char *data = analyzer->buffer;
    size_t size = analyzer->file_size;
    
    // Calculate entropy in blocks
    size_t block_size = 256;
    size_t num_blocks = size / block_size;
    
    printf("Analyzing entropy in %lu blocks of %lu bytes each:\n", num_blocks, block_size);
    
    for (size_t block = 0; block < num_blocks && block < 20; block++) {
        size_t offset = block * block_size;
        unsigned char *block_data = data + offset;
        
        // Calculate frequency
        int freq[256] = {0};
        for (size_t i = 0; i < block_size; i++) {
            freq[block_data[i]]++;
        }
        
        // Calculate entropy
        double entropy = 0.0;
        for (int i = 0; i < 256; i++) {
            if (freq[i] > 0) {
                double p = (double)freq[i] / block_size;
                entropy -= p * log2(p);
            }
        }
        
        printf("  Block %2lu (0x%08lx): ", block, offset);
        
        if (entropy < 1.0) {
            printf(WHITE "%.2f" RESET " - Very low (likely padding)", entropy);
        } else if (entropy < 4.0) {
            printf(YELLOW "%.2f" RESET " - Low (structured data)", entropy);
        } else if (entropy < 6.0) {
            printf(GREEN "%.2f" RESET " - Medium (mixed data)", entropy);
        } else if (entropy < 7.0) {
            printf(CYAN "%.2f" RESET " - High (compressed/encrypted)", entropy);
        } else {
            printf(RED "%.2f" RESET " - Very high (random/encrypted)", entropy);
        }
        printf("\n");
    }
    
    if (num_blocks > 20) {
        printf("  ... (%lu more blocks)\n", num_blocks - 20);
    }
}

// String extraction
void extract_strings(firmware_analyzer_t *analyzer) {
    printf("\n" BOLD GREEN "📝 STRING EXTRACTION 📝\n" RESET);
    printf("═══════════════════════════════════════\n");
    
    unsigned char *data = analyzer->buffer;
    size_t size = analyzer->file_size;
    
    printf("Extracting printable strings (min length 4):\n");
    
    char current_string[256];
    int string_length = 0;
    size_t string_start = 0;
    int strings_found = 0;
    
    for (size_t i = 0; i < size; i++) {
        if (isprint(data[i]) && data[i] != '\t') {
            if (string_length == 0) {
                string_start = i;
            }
            current_string[string_length++] = data[i];
            
            if (string_length >= sizeof(current_string) - 1) {
                current_string[string_length] = '\0';
                printf("  0x%08lx: " GREEN "\"%s...\"" RESET " (truncated)\n", string_start, current_string);
                strings_found++;
                string_length = 0;
            }
        } else {
            if (string_length >= 4) {
                current_string[string_length] = '\0';
                printf("  0x%08lx: " GREEN "\"%s\"" RESET, string_start, current_string);
                
                // Analyze string content
                if (strstr(current_string, "adreno") || strstr(current_string, "ADRENO")) {
                    printf(" " CYAN "- GPU related" RESET);
                } else if (strstr(current_string, "error") || strstr(current_string, "ERROR")) {
                    printf(" " RED "- Error message" RESET);
                } else if (strstr(current_string, "version") || strstr(current_string, "VERSION")) {
                    printf(" " YELLOW "- Version info" RESET);
                } else if (strstr(current_string, "qualcomm") || strstr(current_string, "QUALCOMM")) {
                    printf(" " MAGENTA "- Qualcomm related" RESET);
                }
                
                printf("\n");
                strings_found++;
                
                if (strings_found >= 50) {
                    printf("  ... (limiting output to first 50 strings)\n");
                    break;
                }
            }
            string_length = 0;
        }
    }
    
    printf("\nTotal strings found: %d\n", strings_found);
}

// Instruction pattern analysis (basic)
void analyze_instructions(firmware_analyzer_t *analyzer) {
    printf("\n" BOLD BLUE "🔧 INSTRUCTION ANALYSIS 🔧\n" RESET);
    printf("═══════════════════════════════════════\n");
    
    unsigned char *data = analyzer->buffer;
    size_t size = analyzer->file_size;
    
    printf("Looking for potential ARM/GPU instruction patterns:\n");
    
    // Look for ARM instruction patterns (32-bit, little endian)
    int arm_patterns = 0;
    int gpu_patterns = 0;
    
    for (size_t i = 0; i < size - 4; i += 4) {
        uint32_t word = *((uint32_t*)(data + i));
        
        // ARM instruction patterns
        if ((word & 0xF0000000) == 0xE0000000) {  // Conditional execution
            arm_patterns++;
        }
        
        // GPU/Adreno specific patterns (speculative)
        if ((word & 0xFF000000) == 0x70000000) {  // Potential GPU command
            gpu_patterns++;
            if (gpu_patterns <= 10) {
                printf("  0x%08lx: Potential GPU command: 0x%08x\n", i, word);
            }
        }
    }
    
    printf("\nPattern summary:\n");
    printf("  Potential ARM instructions: %d\n", arm_patterns);
    printf("  Potential GPU commands: %d\n", gpu_patterns);
    
    if (arm_patterns > 100) {
        printf("  " GREEN "Likely contains ARM code" RESET "\n");
    }
    if (gpu_patterns > 10) {
        printf("  " CYAN "Likely contains GPU microcode" RESET "\n");
    }
}

// Main analysis function
void analyze_firmware_file(firmware_analyzer_t *analyzer) {
    printf(BOLD CYAN "🔍 ANALYZING FIRMWARE: %s 🔍\n" RESET, analyzer->filename);
    printf("File size: %lu bytes (%.2f KB)\n", analyzer->file_size, analyzer->file_size / 1024.0);
    printf("═══════════════════════════════════════════════════════════════\n");
    
    // Show first 256 bytes as hex dump
    printf("\n" BOLD "📋 HEX DUMP (First 256 bytes):\n" RESET);
    size_t dump_size = (analyzer->file_size < 256) ? analyzer->file_size : 256;
    print_hex_dump_advanced(analyzer->buffer, dump_size, 0);
    
    // Run all analysis functions
    search_signatures(analyzer);
    analyze_patterns(analyzer);
    analyze_entropy(analyzer);
    extract_strings(analyzer);
    analyze_instructions(analyzer);
    
    printf("\n" BOLD CYAN "═══════════════════════════════════════════════════════════════\n" RESET);
    printf(BOLD "📊 ANALYSIS COMPLETE 📊\n" RESET);
}

// Interactive hex viewer
void interactive_hex_viewer(firmware_analyzer_t *analyzer) {
    printf("\n" BOLD YELLOW "🔍 INTERACTIVE HEX VIEWER 🔍\n" RESET);
    printf("Commands: offset <addr>, search <hex>, strings, quit\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    char command[256];
    unsigned long current_offset = 0;
    
    while (1) {
        printf("\n" CYAN "hex_analyzer> " RESET);
        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = 0;
        
        if (strncmp(command, "offset ", 7) == 0) {
            unsigned long offset = strtoul(command + 7, NULL, 0);
            if (offset < analyzer->file_size) {
                current_offset = offset;
                size_t display_size = (analyzer->file_size - offset < 256) ? 
                                    analyzer->file_size - offset : 256;
                print_hex_dump_advanced(analyzer->buffer + offset, display_size, offset);
            } else {
                printf("Error: Offset 0x%lx beyond file size\n", offset);
            }
        } else if (strncmp(command, "search ", 7) == 0) {
            // Simple hex search
            char *hex_str = command + 7;
            unsigned char search_bytes[32];
            int search_len = 0;
            
            // Parse hex string
            for (int i = 0; hex_str[i] && hex_str[i+1] && search_len < 32; i += 2) {
                if (sscanf(hex_str + i, "%2hhx", &search_bytes[search_len]) == 1) {
                    search_len++;
                } else {
                    break;
                }
            }
            
            if (search_len > 0) {
                printf("Searching for %d bytes starting from 0x%lx...\n", search_len, current_offset);
                int found = 0;
                for (size_t i = current_offset; i <= analyzer->file_size - search_len; i++) {
                    if (memcmp(analyzer->buffer + i, search_bytes, search_len) == 0) {
                        printf("  Found at 0x%08lx\n", i);
                        found++;
                        if (found >= 10) {
                            printf("  ... (limiting to first 10 results)\n");
                            break;
                        }
                    }
                }
                if (found == 0) {
                    printf("  Not found\n");
                }
            } else {
                printf("Error: Invalid hex string\n");
            }
        } else if (strcmp(command, "strings") == 0) {
            extract_strings(analyzer);
        } else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            break;
        } else if (strlen(command) == 0) {
            // Show current location
            size_t display_size = (analyzer->file_size - current_offset < 256) ? 
                                analyzer->file_size - current_offset : 256;
            print_hex_dump_advanced(analyzer->buffer + current_offset, display_size, current_offset);
        } else {
            printf("Unknown command. Available: offset <addr>, search <hex>, strings, quit\n");
        }
    }
}

// Load firmware file
int load_firmware_file(firmware_analyzer_t *analyzer, const char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) {
        printf("Error: Cannot stat file %s: %s\n", filename, strerror(errno));
        return -1;
    }
    
    analyzer->file_size = st.st_size;
    analyzer->filename = strdup(filename);
    
    analyzer->file = fopen(filename, "rb");
    if (!analyzer->file) {
        printf("Error: Cannot open file %s: %s\n", filename, strerror(errno));
        return -1;
    }
    
    analyzer->buffer = malloc(analyzer->file_size);
    if (!analyzer->buffer) {
        printf("Error: Cannot allocate %lu bytes for file\n", analyzer->file_size);
        fclose(analyzer->file);
        return -1;
    }
    
    if (fread(analyzer->buffer, 1, analyzer->file_size, analyzer->file) != analyzer->file_size) {
        printf("Error: Cannot read file completely\n");
        free(analyzer->buffer);
        fclose(analyzer->file);
        return -1;
    }
    
    return 0;
}

void cleanup_analyzer(firmware_analyzer_t *analyzer) {
    if (analyzer->buffer) {
        free(analyzer->buffer);
    }
    if (analyzer->file) {
        fclose(analyzer->file);
    }
    if (analyzer->filename) {
        free(analyzer->filename);
    }
}

int main(int argc, char *argv[]) {
    firmware_analyzer_t analyzer = {0};
    
    printf(BOLD CYAN "🔍💾 ADRENO FIRMWARE HEX ANALYZER 💾🔍\n" RESET);
    printf("Advanced binary analysis for Adreno GPU firmware\n\n");
    
    if (argc < 2) {
        printf("Usage: %s <firmware_file> [interactive]\n", argv[0]);
        printf("       %s adreno_firmware_dump_*.bin\n", argv[0]);
        printf("       %s firmware.bin interactive\n", argv[0]);
        return 1;
    }
    
    if (load_firmware_file(&analyzer, argv[1]) != 0) {
        return 1;
    }
    
    // Perform main analysis
    analyze_firmware_file(&analyzer);
    
    // Check if interactive mode requested
    if (argc > 2 && strcmp(argv[2], "interactive") == 0) {
        interactive_hex_viewer(&analyzer);
    }
    
    cleanup_analyzer(&analyzer);
    
    printf("\n" BOLD GREEN "🎉 Analysis complete! 🎉\n" RESET);
    return 0;
}