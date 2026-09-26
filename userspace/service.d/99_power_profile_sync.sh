#!/system/bin/sh
# Real-time Android OS <-> Linux Kernel Power Profile Sync
# Event-driven synchronization via inotifyd on /data/system/users/0 settings XML files.
# Zero CPU polling overhead; preserves full Deep Sleep state.
#
# Profiles mapping for /proc/sys/kernel/sched_power_profile:
#   GT Mode tile         (gt_mode_state_setting=1)      -> Profile 3 (GT Mode)
#   Super Power Saving   (super_powersave_mode_state=1) -> Profile 0 (Super Power Saving)
#   Power Saving         (global low_power=1)           -> Profile 1 (Power Saving)
#   Normal daily state                                  -> Profile 2 (Balanced)

PROFILE_NODE="/proc/sys/kernel/sched_power_profile"
PPM_CORE_NODE="/proc/ppm/policy/forcelimit_cpu_core"
GED_HAL_MARGIN_NODE="/sys/kernel/ged/hal/dvfs_margin_value"
GPU_MARGIN_NODE="/sys/module/ged/parameters/gx_fb_dvfs_margin"
TARGET_DIR="/data/system/users/0"
BT_STATE_DIR="/data/misc/bluetooth"

(
    # Wait for Android framework to fully boot
    while [ "$(getprop sys.boot_completed)" != "1" ]; do
        sleep 3
    done
    sleep 5

    # Ensure node exists before proceeding
    if [ ! -w "$PROFILE_NODE" ]; then
        log -t power_sync "Kernel does not expose $PROFILE_NODE, exiting"
        exit 0
    fi

    last_profile=-1

    sync_profile() {
        # 1. Check GT Mode (highest priority for gaming)
        gt_val=$(settings get system gt_mode_state_setting 2>/dev/null)
        if [ "$gt_val" = "1" ]; then
            target_profile=3
            profile_name="GT Mode"
        else
            # 2. Check Super Power Saving
            super_val=$(settings get system super_powersave_mode_state 2>/dev/null)
            if [ "$super_val" = "1" ]; then
                target_profile=0
                profile_name="Super Power Saving"
            else
                # 3. Check Standard Power Saving (Battery Saver)
                eco_val=$(settings get global low_power 2>/dev/null)
                if [ "$eco_val" = "1" ]; then
                    target_profile=1
                    profile_name="Power Saving"
                else
                    # 4. Default: Balanced (peak efficiency)
                    target_profile=2
                    profile_name="Balanced"
                fi
            fi
        fi

        # Apply only when changed to avoid needless sysctl writes
        if [ "$target_profile" -ne "$last_profile" ]; then
            if [ "$target_profile" -eq 0 ]; then
                # Super Power Saving: 4 Little + 1 Prime (Mid cores 4-6 offline to shutdown VPROC2 PMIC rail)
                if [ -w "$PPM_CORE_NODE" ]; then
                    printf "%s\n" "-1 -1 0 0 -1 -1" > "$PPM_CORE_NODE" 2>/dev/null
                fi
            else
                # All other profiles (Power Saving, Balanced, GT Mode): restore full 8-core topology
                if [ -w "$PPM_CORE_NODE" ]; then
                    printf "%s\n" "-1 -1 -1 -1 -1 -1" > "$PPM_CORE_NODE" 2>/dev/null
                fi
            fi

            # GPU DVFS headroom: 10% (10 / 100) in Power Saving / Super Power Saving, 15% (15 / 150) in Balanced / GT Mode
            if [ "$target_profile" -eq 0 ] || [ "$target_profile" -eq 1 ]; then
                [ -w "$GED_HAL_MARGIN_NODE" ] && echo "10" > "$GED_HAL_MARGIN_NODE" 2>/dev/null
                [ -w "$GPU_MARGIN_NODE" ] && echo "100" > "$GPU_MARGIN_NODE" 2>/dev/null
            else
                [ -w "$GED_HAL_MARGIN_NODE" ] && echo "15" > "$GED_HAL_MARGIN_NODE" 2>/dev/null
                [ -w "$GPU_MARGIN_NODE" ] && echo "150" > "$GPU_MARGIN_NODE" 2>/dev/null
            fi

            echo "$target_profile" > "$PROFILE_NODE" 2>/dev/null
            log -t power_sync "Switched to profile $target_profile ($profile_name: GT=$gt_val Super=$super_val Eco=$eco_val)"
            last_profile=$target_profile
        fi
    }

    sync_bt_affinity() {
        # 1. Bluetooth A2DP audio encoding and stack worker threads
        bt_pid=$(pidof com.android.bluetooth 2>/dev/null)
        if [ -n "$bt_pid" ]; then
            for task in /proc/$bt_pid/task/*; do
                [ -r "$task/comm" ] || continue
                read -r comm < "$task/comm" 2>/dev/null
                case "$comm" in
                    bt_a2dp_source_*|bt_stack_manage*|bta_media_*|BluetoothA2dp*)
                        tid="${task##*/}"
                        [ -w /dev/cpuset/system-background/tasks ] && echo "$tid" > /dev/cpuset/system-background/tasks 2>/dev/null
                        taskset -p 0f "$tid" >/dev/null 2>&1
                        ;;
                esac
            done
        fi

        # 2. MediaTek Bluetooth Audio HAL service
        hal_pid=$(pidof android.hardware.bluetooth@1.1-service-mediatek 2>/dev/null)
        if [ -n "$hal_pid" ]; then
            [ -w /dev/cpuset/system-background/cgroup.procs ] && echo "$hal_pid" > /dev/cpuset/system-background/cgroup.procs 2>/dev/null
            for task in /proc/$hal_pid/task/*; do
                [ -d "$task" ] || continue
                tid="${task##*/}"
                taskset -p 0f "$tid" >/dev/null 2>&1
            done
        fi

        # 3. Bluetooth Hardware DMA & Wakeup Interrupts (Discrete GICv3 Little Core routing)
        # Note: GICv3 with composite mask 0f uses 1-of-N routing leaking to awake A78 cores.
        # Discrete masks (01, 02, 04, 08) guarantee 100% strict isolation to Cortex-A55.
        [ -w /proc/irq/18/smp_affinity ] && echo 01 > /proc/irq/18/smp_affinity 2>/dev/null   # TX DMA -> Core 0
        [ -w /proc/irq/19/smp_affinity ] && echo 02 > /proc/irq/19/smp_affinity 2>/dev/null   # RX DMA -> Core 1
        [ -w /proc/irq/228/smp_affinity ] && echo 04 > /proc/irq/228/smp_affinity 2>/dev/null # BT Wakeup -> Core 2
        [ -w /proc/irq/17/smp_affinity ] && echo 08 > /proc/irq/17/smp_affinity 2>/dev/null   # BTIF -> Core 3
        [ -w /proc/irq/281/smp_affinity ] && echo 08 > /proc/irq/281/smp_affinity 2>/dev/null # BTCVSD -> Core 3

        # 4. System Housekeeping & Sensor HALs (Health, Power Stats, Thermal -> Hard Little-Core Pinning)
        for b in android.hardware.health@2.1-service android.hardware.power.stats-impl.oplus android.hardware.thermal@2.0-service.mtk; do
            spid=$(pidof "$b" 2>/dev/null)
            if [ -n "$spid" ]; then
                [ -w /dev/cpuset/system-background/cgroup.procs ] && echo "$spid" > /dev/cpuset/system-background/cgroup.procs 2>/dev/null
                for task in /proc/$spid/task/*; do
                    [ -d "$task" ] || continue
                    tid="${task##*/}"
                    taskset -p 0f "$tid" >/dev/null 2>&1
                done
            fi
        done

        # 5. Cellular Radio Interface Layer (mtkfusionrild -> Soft System-Background Migration)
        ril_pid=$(pidof mtkfusionrild 2>/dev/null)
        if [ -n "$ril_pid" ]; then
            [ -w /dev/cpuset/system-background/cgroup.procs ] && echo "$ril_pid" > /dev/cpuset/system-background/cgroup.procs 2>/dev/null
        fi
    }

    # Initial synchronization on boot
    sync_profile
    sync_bt_affinity

    # Pure event-driven watcher on system settings and Bluetooth state updates (Zero-Polling)
    # Filter :cy wakes strictly on close_write (c) and moved_to (y), preventing read-loop amplification.
    while true; do
        inotifyd - "$TARGET_DIR:cy" "$BT_STATE_DIR:cy" 2>/dev/null | while read -r event dir file; do
            case "$file" in
                settings_system.xml|settings_global.xml)
                    sync_profile
                    ;;
                cached_hci*|*bt*|*blue*)
                    sync_bt_affinity
                    ;;
            esac
        done
        sleep 2
    done
) &
