#!/bin/bash

# 🔥 ULTIMATE ADRENO ARSENAL DASHBOARD 🔥
# Real-time GPU exploitation monitoring

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
NC='\033[0m'

# Get GPU stats
get_gpu_temp() {
    cat /sys/class/kgsl/kgsl-3d0/temp 2>/dev/null | awk '{print int($1/1000)}'
}

get_gpu_freq() {
    cat /sys/class/kgsl/kgsl-3d0/clock_mhz 2>/dev/null
}

get_gpu_util() {
    cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage 2>/dev/null | sed 's/ %//'
}

get_gpu_resets() {
    cat /sys/class/kgsl/kgsl-3d0/reset_count 2>/dev/null
}

# ASCII GPU visualization
draw_gpu() {
    local temp=$1
    local freq=$2
    local util=$3
    
    echo -e "${CYAN}"
    echo "    ╔══════════════════════════════════════╗"
    echo "    ║            ADRENO 730v2              ║"
    echo "    ║    Galaxy S22 GPU STATUS BOARD      ║"
    echo "    ╠══════════════════════════════════════╣"
    
    # Temperature bar
    local temp_color="${GREEN}"
    if [ "$temp" -gt 70 ]; then temp_color="${YELLOW}"; fi
    if [ "$temp" -gt 85 ]; then temp_color="${RED}"; fi
    
    printf "    ║ 🌡️  Temp: ${temp_color}%3d°C${CYAN} "
    for i in $(seq 1 20); do
        if [ $((temp * 20 / 100)) -ge $i ]; then
            printf "█"
        else
            printf "░"
        fi
    done
    printf " ║\n"
    
    # Frequency bar  
    local freq_percentage=$((freq * 100 / 818))  # 818 MHz max
    printf "    ║ ⚡ Freq: ${WHITE}%3d MHz${CYAN} "
    for i in $(seq 1 20); do
        if [ $((freq_percentage * 20 / 100)) -ge $i ]; then
            printf "█"
        else
            printf "░"
        fi
    done
    printf " ║\n"
    
    # Utilization bar
    local util_color="${GREEN}"
    if [ "$util" -gt 50 ]; then util_color="${YELLOW}"; fi
    if [ "$util" -gt 80 ]; then util_color="${RED}"; fi
    
    printf "    ║ 📊 Util: ${util_color}%3d%%${CYAN}   "
    for i in $(seq 1 20); do
        if [ $((util * 20 / 100)) -ge $i ]; then
            printf "█"
        else
            printf "░"
        fi
    done
    printf " ║\n"
    
    echo "    ╚══════════════════════════════════════╝"
    echo -e "${NC}"
}

# Exploit status
show_exploit_status() {
    echo -e "${PURPLE}┌─────────────────────────────────────────────┐${NC}"
    echo -e "${PURPLE}│              EXPLOIT STATUS                 │${NC}"
    echo -e "${PURPLE}├─────────────────────────────────────────────┤${NC}"
    
    # Check if exploits exist and are executable
    local exploits_dir="./exploits"
    
    if [ -x "$exploits_dir/final_injector" ]; then
        echo -e "${PURPLE}│ ${GREEN}✓${PURPLE} Command Injection:     ${GREEN}ARMED${PURPLE}        │${NC}"
    else
        echo -e "${PURPLE}│ ${RED}✗${PURPLE} Command Injection:     ${RED}OFFLINE${PURPLE}      │${NC}"
    fi
    
    if [ -x "$exploits_dir/ring_buffer_hijack" ]; then
        echo -e "${PURPLE}│ ${GREEN}✓${PURPLE} Ring Buffer Hijack:    ${GREEN}READY${PURPLE}        │${NC}"
    else
        echo -e "${PURPLE}│ ${RED}✗${PURPLE} Ring Buffer Hijack:    ${RED}OFFLINE${PURPLE}      │${NC}"
    fi
    
    if [ -x "$exploits_dir/context_bypass" ]; then
        echo -e "${PURPLE}│ ${GREEN}✓${PURPLE} Context Bypass:        ${GREEN}LOADED${PURPLE}       │${NC}"
    else
        echo -e "${PURPLE}│ ${RED}✗${PURPLE} Context Bypass:        ${RED}OFFLINE${PURPLE}      │${NC}"
    fi
    
    if [ -x "$exploits_dir/firmware_explorer" ]; then
        echo -e "${PURPLE}│ ${GREEN}✓${PURPLE} Firmware Explorer:     ${GREEN}ACTIVE${PURPLE}       │${NC}"
    else
        echo -e "${PURPLE}│ ${RED}✗${PURPLE} Firmware Explorer:     ${RED}OFFLINE${PURPLE}      │${NC}"
    fi
    
    if [ -x "$exploits_dir/gpu_weapon" ]; then
        echo -e "${PURPLE}│ ${GREEN}✓${PURPLE} GPU Weapon System:     ${GREEN}ARMED${PURPLE}        │${NC}"
    else
        echo -e "${PURPLE}│ ${RED}✗${PURPLE} GPU Weapon System:     ${RED}OFFLINE${PURPLE}      │${NC}"
    fi
    
    if [ -x "./gpu_memory_spy" ]; then
        echo -e "${PURPLE}│ ${GREEN}✓${PURPLE} Memory Spy:            ${GREEN}ACTIVE${PURPLE}       │${NC}"
    else
        echo -e "${PURPLE}│ ${RED}✗${PURPLE} Memory Spy:            ${RED}OFFLINE${PURPLE}      │${NC}"
    fi
    
    echo -e "${PURPLE}└─────────────────────────────────────────────┘${NC}"
}

# Live attack statistics
show_attack_stats() {
    local log_dir="./logs"
    local latest_log=""
    
    if [ -d "$log_dir" ]; then
        latest_log=$(ls -t "$log_dir"/campaign_*.log 2>/dev/null | head -1)
    fi
    
    echo -e "${YELLOW}┌─────────────────────────────────────────────┐${NC}"
    echo -e "${YELLOW}│             ATTACK STATISTICS               │${NC}"
    echo -e "${YELLOW}├─────────────────────────────────────────────┤${NC}"
    
    if [ -n "$latest_log" ]; then
        local campaign_time=$(stat -c %Y "$latest_log" 2>/dev/null)
        local current_time=$(date +%s)
        local time_diff=$((current_time - campaign_time))
        
        echo -e "${YELLOW}│ Last Campaign: ${WHITE}$(basename "$latest_log")${YELLOW}     │${NC}"
        echo -e "${YELLOW}│ Time Ago:      ${WHITE}${time_diff}s${YELLOW}                   │${NC}"
        echo -e "${YELLOW}│ Status:        ${GREEN}COMPLETED${YELLOW}              │${NC}"
    else
        echo -e "${YELLOW}│ Last Campaign: ${RED}NONE${YELLOW}                    │${NC}"
        echo -e "${YELLOW}│ Status:        ${RED}STANDBY${YELLOW}                 │${NC}"
    fi
    
    # Count total exploits
    local total_exploits=$(ls ./exploits/ 2>/dev/null | grep -v '\.c$' | wc -l)
    echo -e "${YELLOW}│ Total Exploits: ${WHITE}${total_exploits}${YELLOW}                  │${NC}"
    
    # Check GPU resets
    local resets=$(get_gpu_resets)
    echo -e "${YELLOW}│ GPU Resets:     ${WHITE}${resets}${YELLOW}               │${NC}"
    
    echo -e "${YELLOW}└─────────────────────────────────────────────┘${NC}"
}

# Main dashboard loop
main_dashboard() {
    while true; do
        clear
        
        # Header
        echo -e "${RED}"
        echo "  ╔══════════════════════════════════════════════════════════════════╗"
        echo "  ║                  🔥 ADRENO ARSENAL DASHBOARD 🔥                  ║"
        echo "  ║                   Real-time GPU Exploitation                     ║"
        echo "  ║                      Galaxy S22 Command Center                   ║"
        echo "  ╚══════════════════════════════════════════════════════════════════╝"
        echo -e "${NC}"
        
        # Get current GPU stats
        local temp=$(get_gpu_temp)
        local freq=$(get_gpu_freq)
        local util=$(get_gpu_util)
        
        # Display GPU visualization
        draw_gpu "$temp" "$freq" "$util"
        
        echo ""
        
        # Display exploit status and attack stats side by side
        {
            show_exploit_status
        } &
        
        {
            echo ""
            show_attack_stats
        } &
        
        wait
        
        echo ""
        echo -e "${CYAN}┌─────────────────────────────────────────────┐${NC}"
        echo -e "${CYAN}│                QUICK ACTIONS                │${NC}"
        echo -e "${CYAN}├─────────────────────────────────────────────┤${NC}"
        echo -e "${CYAN}│ ${WHITE}[1]${CYAN} Launch Memory Spy                  │${NC}"
        echo -e "${CYAN}│ ${WHITE}[2]${CYAN} Run GPU Destroyer (30s)            │${NC}"
        echo -e "${CYAN}│ ${WHITE}[3]${CYAN} Full Arsenal Campaign               │${NC}"
        echo -e "${CYAN}│ ${WHITE}[4]${CYAN} GPU Stress Test                     │${NC}"
        echo -e "${CYAN}│ ${WHITE}[5]${CYAN} View Logs                           │${NC}"
        echo -e "${CYAN}│ ${WHITE}[q]${CYAN} Quit Dashboard                      │${NC}"
        echo -e "${CYAN}└─────────────────────────────────────────────┘${NC}"
        
        echo -e "\n${WHITE}Current time: $(date)${NC}"
        echo -e "${GREEN}Dashboard refreshing every 5 seconds...${NC}"
        echo -e "${RED}Press Ctrl+C to stop or 'q' to quit${NC}"
        
        # Wait for input or timeout
        read -t 5 -n 1 action
        
        case "$action" in
            "1")
                clear
                echo -e "${YELLOW}🕵️ Launching Memory Spy...${NC}"
                ./gpu_memory_spy
                read -p "Press Enter to return to dashboard..."
                ;;
            "2")
                clear
                echo -e "${RED}💥 Launching GPU Destroyer...${NC}"
                ./gpu_destroyer 30
                read -p "Press Enter to return to dashboard..."
                ;;
            "3")
                clear
                echo -e "${PURPLE}⚔️ Launching Full Arsenal...${NC}"
                ./arsenal_manager.sh campaign "Dashboard_Attack" "2"
                read -p "Press Enter to return to dashboard..."
                ;;
            "4")
                clear
                echo -e "${BLUE}📊 Running GPU Stress Test...${NC}"
                ./exploits/adreno_control monitor 30
                read -p "Press Enter to return to dashboard..."
                ;;
            "5")
                clear
                echo -e "${CYAN}📋 Recent Logs:${NC}"
                ls -la ./logs/ 2>/dev/null || echo "No logs found"
                read -p "Press Enter to return to dashboard..."
                ;;
            "q"|"Q")
                echo -e "\n${GREEN}Dashboard shutdown. Arsenal standing by.${NC}"
                exit 0
                ;;
        esac
    done
}

# Check dependencies
check_dependencies() {
    if [ ! -d "./exploits" ]; then
        echo -e "${RED}Error: Exploits directory not found!${NC}"
        echo -e "${YELLOW}Run: ./arsenal_manager.sh init${NC}"
        exit 1
    fi
    
    if [ ! -f "/sys/class/kgsl/kgsl-3d0/temp" ]; then
        echo -e "${RED}Error: GPU not accessible!${NC}"
        exit 1
    fi
}

# Main execution
check_dependencies

echo -e "${GREEN}🔥 Starting Adreno Arsenal Dashboard...${NC}"
sleep 2

main_dashboard