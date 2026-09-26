# NeoCore Kernel for Realme GT Neo 2T (RMX3357 / MT6893)

[![Kernel Version](https://img.shields.io/badge/Linux-4.19.191-blue.svg)](https://kernel.org/)
[![Device](https://img.shields.io/badge/Device-Realme%20GT%20Neo%202T%20(RMX3357)-brightgreen.svg)](https://github.com/realme-kernel-opensource/realme_X7max_GTneo_GTneo2T-AndroidT-kernel-source)
[![KernelSU-Next](https://img.shields.io/badge/KernelSU--Next-v3.4.0--legacy-success.svg)](https://github.com/rifsxd/KernelSU-Next)
[![SuSFS](https://img.shields.io/badge/SuSFS-v1.5.5-blueviolet.svg)](https://gitlab.com/simonpunk/susfs4ksu)

[English](#english) | [Русский](#русский)

---

<a name="english"></a>
## English

Custom Linux 4.19.191 kernel for **Realme GT Neo 2T** (`RMX3357` / `RE5469`) on **MediaTek Dimensity 1200 5G** (`MT6893`).  
Built for stock firmware **`RMX3357_13.1.0.500(CN01)`** (realme UI 4.0 / Android 13). Version: **v2026.09.27**.

**Key Highlights:**
- Built-in stealth root via in-tree KernelSU-Next and SuSFS 1.5.5 with hardware safe mode.
- Fixes stock Dimensity 1200 schedutil governor overheating and battery drain.
- Sustains 120 Hz display fluidness without microstutter or thermal throttling.
- Reduces background battery drain during Bluetooth audio playback and isolates system telemetry.
- Expands networking, storage, audio, eBPF tracing, and hardware diagnostic capabilities.

**Table of Contents:**
- [Quick Start & Rollback](#quick-start--rollback)
- [Stealth & Root (KernelSU-Next + SuSFS 1.5.5)](#stealth--root-kernelsu-next--susfs-155)
- [Energy Efficiency, Smoothness & Thermal Control](#energy-efficiency-smoothness--thermal-control)
- [Network, Audio, Storage, Peripherals & Hardware Compatibility](#network-audio-storage-peripherals--hardware-compatibility)
- [System Requirements & Compatibility](#system-requirements--compatibility)
- [Building from Source](#building-from-source)
- [Sources & Acknowledgments](#sources--acknowledgments)

---

### Quick Start & Rollback

> [!IMPORTANT]
> Unlock your bootloader and back up your stock `boot.img` before flashing.

#### 1. Flash Kernel via Fastboot
1. Boot into the bootloader:
   ```bash
   adb reboot bootloader
   ```
2. Verify device connection:
   ```bash
   fastboot devices
   ```
3. Flash the NeoCore boot image:
   ```bash
   fastboot flash boot boot-rmx3357-neocore.img
   ```
4. Reboot into system:
   ```bash
   fastboot reboot
   ```

#### 2. Install Root & Companion Module
1. Install [KernelSU-Next Manager APK (v3.4.0)](https://github.com/rifsxd/KernelSU-Next/releases).
2. Open KernelSU Manager (or Magisk) and flash the companion module: **`rmx3357-neocore-companion.zip`** (source files in [`userspace/`](userspace/)).
3. Reboot to activate userspace optimizations (ColorOS power profile sync, Bluetooth Little-core affinity, MediaTek logger cleanup).

#### 3. Hardware Safe Mode
If an installed module prevents booting, press **Volume Down** (`KEY_VOLUMEDOWN`) three times consecutively during phone startup. The kernel input driver intercepts this sequence and disables all modules.

#### 4. Rollback to Stock
To revert to the stock kernel at any time:
```bash
adb reboot bootloader
fastboot flash boot boot_stock.img
fastboot reboot
```

---

### Stealth & Root (KernelSU-Next + SuSFS 1.5.5)

#### 1. In-Tree KernelSU-Next v3.4.0-legacy (UAPI 4)
- **Direct Kernel Integration**: Integrated directly into `drivers/kernelsu` (`CONFIG_KSU_MANUAL_HOOK=y`), avoiding unstable dynamic `kprobes` and hook failures.
- **Scoped Descriptors**: Operates over UAPI v4 scoped file descriptors (`[ksu_driver_su]`), granting root privileges only to processes possessing an authenticated descriptor issued by the manager.

#### 2. SuSFS 1.5.5 Kernel Stealth Subsystem
- **Clean VFS Architecture**: Standard VFS code paths without inline path-redirection hacks, preserving POSIX file locking semantics and preventing SQLite database corruption.
- **Mount & Dentry Hiding**: Hidden mounts (`add_sus_mount` in `fs/namespace.c`) are filtered out of `/proc/mounts`, `/proc/mountinfo`, and `/proc/mountstats`. RCU fast path `__d_lookup_rcu()` in `fs/dcache.c` invokes `susfs_is_current_task_sus_blocked()` to skip hidden dentries inside RCU critical sections.
- **EROFS statfs Spoofing**: Function `zeromount_spoof_statfs` for read-only system partitions (`/system`, `/vendor`, `/product`) spoofs filesystem statistics (e.g. 0 free blocks, magic `0xe0f5e1e2`), completely eliminating detection of read-only root and overlay mounts.
- **Maps, Descriptors & Symbol Cloaking**: Masks module paths in `/proc/[pid]/maps` and strips inotify watch descriptors in `/proc/[pid]/fdinfo/`. `CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS` in `kernel/kallsyms.c` strips `ksu_`, `susfs_`, and `ksud` symbols. `sys_newuname` in `kernel/sys.c` spoofs certified stock release strings for passing Play Integrity checks.

#### 3. Hybrid Mount & NeoZygisk Integration
- Fully compatible with [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) (`disable_umount = true`) for clean mount isolation without unmount deadlocks, and [NeoZygisk](https://github.com/JingMatrix/NeoZygisk) as the modern Zygisk provider.

#### 4. Hardware Safe Mode (Vol- x3)
- If an installed module causes a bootloop, pressing **Volume Down** (`KEY_VOLUMEDOWN`) three times consecutively during device power-on triggers an emergency safe mode directly inside `drivers/input/input.c`, disabling all module loading and restoring bootability.

---

### Energy Efficiency, Smoothness & Thermal Control

#### 1. Energy-Efficiency Profiles & Frequency Caps in `schedutil`
On stock realme UI, the MediaTek schedutil governor aggressively ramps Cortex-A78 cores up to 2.6–3.0 GHz even for simple social media scrolling or background tasks. This spikes active CPU power up to ~3 W, heats up the phone, and triggers severe thermal throttling. Stock battery saver either shuts down cores (causing hotplug stutter) or caps clocks too low, breaking 120 Hz fluidness.

NeoCore enforces lockless hardware frequency ceilings per cluster directly inside `get_next_freq()` (`kernel/sched/cpufreq_schedutil.c`) via `smp_load_acquire`. The scheduler reacts instantly to load, but prevents cores from climbing into the power-hungry top range of the TSMC N6 voltage/frequency curve.

- **Hardware Touch Boost**: Intercepts `EV_ABS` and `BTN_TOUCH` hardware events in `drivers/input/input.c`, issuing a 35 ms boost pulse for instant UI responsiveness.
- **Manual CLI Control**: Switch profiles on the fly from a root shell:
  ```bash
  # Check active profile (0=Super Power Saving, 1=Power Saving, 2=Balanced, 3=GT)
  cat /proc/sys/kernel/sched_power_profile

  # Switch to Balanced (recommended default)
  su -c sysctl -w kernel.sched_power_profile=2

  # Switch to Super Power Saving (maximum battery conservation)
  su -c sysctl -w kernel.sched_power_profile=0

  # Switch to GT Mode (uncapped clocks for gaming)
  su -c sysctl -w kernel.sched_power_profile=3
  ```

| Profile ID | Profile Name | Little Cluster (CPUs 0–3) | Mid Cluster (CPUs 4–6) | Prime Core (CPU 7) | Architectural Target & Benefits |
| :---: | :--- | :---: | :---: | :---: | :--- |
| `0` | **Super Power Saving** | 1.625 GHz @ 906 mV | Offline (PPM) | 1.998 GHz @ 875 mV | **4 Little + 1 Prime** topology. Mid cores offline, `VPROC2` power rail shut down. 10% GPU margin, 66 mA idle, 547 mW peak. Maximum battery conservation. |
| `1` | **Power Saving** | 1.625 GHz @ 906 mV | 1.985 GHz @ 919 mV | 2.141 GHz @ 900 mV | Battery saver with all 8 cores active. `-20.6%` active power vs Balanced, 10% GPU margin. Zero hotplug lag while keeping full 120 Hz smoothness. |
| `2` | **Balanced** *(Default)* | 1.800 GHz @ 950 mV | 2.354 GHz @ 1000 mV | 2.463 GHz @ 956 mV | Physical sweet spot on TSMC N6. 15% GPU margin (`dvfs_margin_value=15`, `gx_fb_dvfs_margin=150`). Sustained 120 Hz load without overheating. |
| `3` | **GT Mode (Uncapped)** | 2.000 GHz (Max) | 2.600 GHz (Max) | 3.000 GHz (Max) | Full 8-core performance unlocked for gaming, heavy compute, and compiling (2979 mW peak, 15% GPU margin). |

#### 2. Harmonic Display Timer (`CONFIG_HZ=360`)
Stock Android kernels run at 100 Hz, 250 Hz, or 300 Hz, which do not divide cleanly into 120 Hz frame intervals (8.333 ms). NeoCore uses `CONFIG_HZ=360` with a tick period of $1000 / 360 \approx 2.777\text{ ms}$:
- Exactly 3 ticks fit into an 8.333 ms frame (120 Hz).
- Exactly 6 ticks fit into a 16.666 ms frame (60 Hz).  
Scheduler accounting aligns with display vblank deadlines, eliminating phase drift and frame drops (zero vblank jitter).

#### 3. Three-Layer Bluetooth LDAC Little-Core Isolation
High-bitrate Bluetooth codecs (LDAC at 990 kbps) cause heavy battery drain on stock realme UI because audio threads and interrupts constantly wake up Cortex-A78 cores:
- **Layer 1 (Audio Stack & Codec Threads)**: `com.android.bluetooth` threads (`bt_a2dp_source_` [LDAC encoder], `bt_stack_manage`, `BluetoothA2dpLo`, `bta_media_*`) are moved to `/dev/cpuset/system-background/tasks` and pinned to cores 0–3 (`taskset -p 0f`).
- **Layer 2 (Vendor HAL Service)**: MediaTek Bluetooth Audio HAL (`android.hardware.bluetooth@1.1-service-mediatek`) is migrated to `/dev/cpuset/system-background/cgroup.procs` with all worker threads pinned to Little cores (`taskset -p 0f`).
- **Layer 3 (ARM GICv3 Hardware Interrupt Routing)**: Eliminates 1-of-N routing interrupt leakage from composite mask `0f` (~23.7% leakage to Cortex-A78 big cores) by mapping IRQs with discrete bitwise masks strictly to Cortex-A55 Little cores:
  - IRQ 18 (`mtk btif tx dma irq`) -> Core 0 (`smp_affinity: 01`)
  - IRQ 19 (`mtk btif rx dma irq`) -> Core 1 (`smp_affinity: 02`)
  - IRQ 228 (`BTIF_WAKEUP_IRQ`) -> Core 2 (`smp_affinity: 04`)
  - IRQ 17 (`mtk btif irq`) & IRQ 281 (`BTCVSD_ISR_Handle`) -> Core 3 (`smp_affinity: 08`)
- **Result**: Cores 4–7 maintain strictly 0 interrupts during Deep Sleep. LDAC playback battery drain drops 4.5–5× (from 18–24%/hr down to 3.5–5%/hr), Little cores idle at 600 MHz, and the device stays cool (26.2 °C).

#### 4. ColorOS Power Mode Auto-Synchronization (`power_sync.sh`)
An asynchronous event-driven daemon based on Toybox `inotifyd` with `:cy` (`close_write` / `moved_to`) filtering across `/data/system/users/0` (`settings_system.xml`, `settings_global.xml`) and `/data/misc/bluetooth`:
- **Zero Overhead**: Does not poll. The daemon blocks in kernel `read()`, consuming 0.0% CPU and zero wakeups during idle.
- **Automatic Mapping**:
  - ColorOS Super Power Saving (`super_powersave_mode_state=1`) -> Profile `0`
  - ColorOS Battery Saver (`low_power=1`) -> Profile `1`
  - ColorOS Balanced Mode -> Profile `2`
  - ColorOS GT Mode (`gt_mode_state_setting=1`) or Gaming Space -> Profile `3`
- **Dynamic Adjustments**: Automatically updates GED GPU DVFS headroom (`/sys/kernel/ged/hal/dvfs_margin_value=10` or `15`) and offlines Mid cores via MediaTek PPM (`/proc/ppm/policy/forcelimit_cpu_core`) in Super Power Saving mode.

#### 5. Telemetry & MediaTek Spam Logger Cleanup
The companion service (`service.sh`) stops noisy MediaTek debug loggers and ColorOS crash uploaders:
- Stops: `emdlogger`, `connsyslogger`, `mobile_log_d`, `bt_dump`, `wifi_dump`, `netdiag`, `criticallog`, `common_dcs`, `phoenix_log_manager`.
- Keeps: `oiface` running to ensure game frame-rate stabilization and GPU DVFS remain fully functional.

---

### Network, Audio, Storage, Peripherals & Hardware Compatibility

#### 1. High-Speed USB CDC NCM Tethering
- **USB CDC NCM Tethering**: Replaces deprecated RNDIS (`CONFIG_USB_CONFIGFS_RNDIS=n`) with CDC NCM (`CONFIG_USB_CONFIGFS_NCM=y`, `CONFIG_USB_NET_CDC_NCM=y`). Lowers CPU load and increases throughput over USB tethering.
- **NTB Buffer Flush Fix**: Multi-packet USB gadgets like CDC NCM (`f_ncm.c`) flush pending Network Transfer Blocks by passing `ndo_start_xmit(NULL, dev)`. Stock `drivers/usb/gadget/function/u_ether.c` rejected `skb == NULL` with `-EINVAL`. Accepting null packets and moving pointer setup past `dev->wrap()` fixes buffer flushing and prevents network stalls under gigabit traffic.
- **Vendor Init Compatibility**: Aliased to `rndis` in `drivers/usb/gadget/function/f_ncm.c` so stock `init.rc` scripts configure tethering without modification.

#### 2. Network Stack & Proxies (Bufferbloat, BBR, WireGuard, TPROXY, SMB3)
- **Bufferbloat Mitigation**: Default queue discipline is tuned to **FQ-CoDel** (`CONFIG_DEFAULT_NET_SCH="fq_codel"`). In-tree Google BBR v1 (`CONFIG_TCP_CONG_BBR=y`), Westwood+, and CAKE are compiled in and selectable via sysctl.
- **Transparent Proxies & Hotspot Limits**: `CONFIG_NETFILTER_XT_TARGET_TPROXY=y` allows redirecting traffic for transparent proxies (Sing-box, Clash, Xray). Netfilter `TTL` and `HL` targets bypass mobile carrier tethering restrictions:
  ```bash
  # Fix outbound IPv4 TTL to 64
  su -c iptables -t mangle -A POSTROUTING -j TTL --ttl-set 64
  # Fix outbound IPv6 Hop Limit to 64
  su -c ip6tables -t mangle -A POSTROUTING -j HL --hl-set 64
  ```
- **In-Tree WireGuard & Containers**: Kernel-native WireGuard (`CONFIG_WIREGUARD=y`), Linux namespaces (`USER_NS`, `PID_NS`, `NET_NS`), cgroups, `veth`, and bridge support for container workloads.
- **Direct SMB3 Mounts**: Direct in-kernel mounting of SMB3 / CIFS shares (`CONFIG_CIFS=y`, `CONFIG_NETWORK_FILESYSTEMS=y`). NFS is disabled (`CONFIG_NFS_FS=n`) to maintain Google Android 13 VINTF compatibility:
  ```bash
  su -c mkdir -p /mnt/share
  su -c mount -t cifs //192.168.1.100/Data /mnt/share \
      -o username=user,password=pass,vers=3.0,iocharset=utf8
  ```
- **TCP Network Stack Tuning**: Enforces Cubic (`tcp_congestion_control=cubic`), enables TCP Fast Open (`tcp_fastopen=3`), sets safe ECN response mode (`tcp_ecn=2`) to prevent carrier CGNAT blackholing, and disables `tcp_slow_start_after_idle`.

#### 3. Bitperfect Audio, USB DAC Mode, ALSA Loopback & MIDI
- **Bitperfect USB Audio**: Native UAC1/UAC2 (`CONFIG_SND_USB_AUDIO=y`) with asynchronous endpoints and direct DSD/DoP playback without Android audio HAL resampling.
- **USB Audio Gadget**: Phone can act as a USB DAC sound card for a PC (`CONFIG_USB_CONFIGFS_F_UAC2=y`).
- **ALSA Loopback & MIDI**: Virtual sound card loopback (`CONFIG_SND_ALOOP=m`) and USB MIDI (`CONFIG_SND_RAWMIDI=y`, `CONFIG_USB_CONFIGFS_F_MIDI=y`).

#### 4. Hardware Peripherals & Diagnostics (SocketCAN, SDR, UART, Wi-Fi 6 Monitor Mode)
- **Automotive SocketCAN (OBD-II)**: Native CAN bus support (`slcan`, `vcan`, `can_raw`) for vehicle diagnostics:
  ```bash
  # Initialize CAN adapter at 500 kbps (OBD-II standard)
  su -c slcand -o -c -s6 /dev/ttyUSB0 slcan0
  su -c ip link set slcan0 up
  su -c candump slcan0
  ```
- **Software-Defined Radio (SDR)**: Realtek RTL2832U DVB-T driver (`CONFIG_DVB_USB_RTL28XXU=m`) for SDR spectrum reception.
- **UART Serial Drivers**: In-tree drivers for CH340/CH341, CP2102/CP2104, FTDI FT232, and PL2303 chips.
- **Wireless Pentesting (Wi-Fi 6 / mac80211)**: Modular `mac80211` stack (`CONFIG_MAC80211=m`) with monitor mode and packet injection on external USB adapters (Atheros AR9271, Ralink RT3070, Realtek RTL8187).

#### 5. Storage Queue Tuning & Runtime Optimization (UFS 3.1 & ART Heap)
- **UFS 3.1 Block Queue Optimization**: Tunes `sd*` queues on boot (`cfq` scheduler, `nr_requests=256`, `read_ahead_kb=256`, `rq_affinity=1`, `nomerges=0`, `add_random=0`).
- **Multi-Queue Elevator Unblocking**: Removes vendor restriction in `elevator_init_mq()` (`block/elevator.c`), allowing multi-queue block devices to use `mq-deadline` and `kyber` instead of forcing `none`.
- **ART Dalvik VM Heap**: Sets heap bounds in `system.prop` via `resetprop` prior to Zygote initialization (`heapstartsize=12m`, `heapgrowthlimit=512m`, `heapsize=768m`), preventing garbage collection stutter and early background app kills.
- **HAL & Modem Isolation**: Hardware health, power stats, and thermal HALs are pinned to Little cores to stop them from waking A78 cores during battery/thermal polling. Cellular RIL (`mtkfusionrild`) is given soft Little-core priority; high-speed 4G/5G data routing remains in the kernel and modem DSP.

#### 6. Vendor Driver & Hardware Compatibility (MediaTek KABI Invariants)
- **Pinned Struct Offsets**: Layouts for `net_device` (`dev_addr` offset strictly **752** bytes / `0x2f0`) and `task_struct` (`thread` offset strictly **3168** bytes, enforced via `BUILD_BUG_ON`) match stock offsets byte-for-byte, preventing memory corruption in proprietary MediaTek modules.
- **Symbol CRC Tolerance**: `check_version()` in `kernel/module.c` sets `TAINT_FORCED_MODULE` and returns success on symbol CRC differences instead of rejecting load with `-ENOEXEC`. `module_sig_check()` relaxes strict module signature enforcement, allowing closed-source vendor blobs to load cleanly.
- **Exported Wi-Fi 6 Bridge**: `wmt_build_in_adapter.c` exports `conn_dbg_add_log`, satisfying dynamic linking requirements for the closed-source `wlan_drv_gen4m.ko` Wi-Fi 6 driver.
- **SELinux NULL Guards**: `selinux_inode()` and `selinux_cred()` in `security/selinux/include/objsec.h` prevent NULL pointer panics when evaluating permissions on synthetic dentries created by OverlayFS, KernelSU, or SuSFS.

#### 7. eBPF Subsystem, Hardware JIT & In-Kernel Tracing
- **Hardware-Enforced BPF JIT**: In-tree AArch64 JIT compiler (`CONFIG_BPF_JIT=y`, `arch/arm64/net/bpf_jit_comp.c`) with interpreter removal (`CONFIG_BPF_JIT_ALWAYS_ON=y`) for native execution speed and Spectre v2 mitigation.
- **Android Traffic Accounting & cgroups**: `CONFIG_CGROUP_BPF=y`, `CONFIG_NET_CLS_BPF=y`, and `CONFIG_NETFILTER_XT_MATCH_BPF=y` support Android `netd` per-UID network monitoring, traffic classification, and bandwidth limits.
- **Dynamic Tracing & Probes**: Kernel-level kprobes, uprobes, and perf events (`CONFIG_BPF_EVENTS=y`, `CONFIG_KPROBE_EVENTS=y`, `CONFIG_UPROBE_EVENTS=y`) allow attaching eBPF programs for latency profiling, I/O analysis, and scheduler telemetry.
- **4.19 Architecture Bounds**: Standard Linux 4.19 baseline uses manual VFS hooks for root isolation (`CONFIG_KSU_MANUAL_HOOK=y`). BPF LSM and vmlinux BTF (CO-RE) are omitted to preserve MediaTek KABI stability and prevent boot image bloat.

---

### System Requirements & Compatibility

- **Device**: Realme GT Neo 2T (`RMX3357` / `RE5469`).
- **Platform**: MediaTek Dimensity 1200 5G (`MT6893`).
- **Firmware Base**: Stock realme UI 4.0 / ColorOS 13.1 on Android 13 (`RMX3357_13.1.0.500(CN01)`).
- **Bootloader**: Unlocked bootloader required.

---

### Building from Source

- **Toolchain**: AOSP Clang 12.0.5 (`clang-r416183b`), LLD 12.0.5, GNU binutils 2.34 (`aarch64-linux-gnu-`, `arm-linux-gnueabi-`).
- **Base Defconfig**: `arch/arm64/configs/k6893v1_64_k419_defconfig` (or `k6893v1_64_k419_ab_defconfig` for A/B).
- **Config Overlay**: `arch/arm64/configs/custom_rmx3357.config`.

```bash
export ARCH=arm64
export SUBARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export CROSS_COMPILE_ARM32=arm-linux-gnueabi-
export CC=clang
export CLANG_TRIPLE=aarch64-linux-gnu-

# Generate base configuration
make k6893v1_64_k419_defconfig

# Apply custom configuration overlay
cat arch/arm64/configs/custom_rmx3357.config >> .config
make olddefconfig

# Build kernel image and modules
make -j$(nproc) Image.gz modules
```

---

### Sources & Acknowledgments

- [Realme Open Source Kernel Base](https://github.com/realme-kernel-opensource/realme_X7max_GTneo_GTneo2T-AndroidT-kernel-source)
- [Realme Open Source Vendor Base](https://github.com/realme-kernel-opensource/realme_X7max_GTneo_GTneo2T-AndroidT-vendor-source)
- [KernelSU-Next](https://github.com/rifsxd/KernelSU-Next) — [@rifsxd](https://github.com/rifsxd)
- [SuSFS](https://gitlab.com/simonpunk/susfs4ksu) — [@simonpunk](https://gitlab.com/simonpunk) & [@sidex102](https://github.com/sidex102)
- [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) — [Hybrid-Mount](https://github.com/Hybrid-Mount)
- [NeoZygisk](https://github.com/JingMatrix/NeoZygisk) — [@JingMatrix](https://github.com/JingMatrix)

---

<a name="русский"></a>
## Русский

Кастомное ядро Linux 4.19.191 для **Realme GT Neo 2T** (`RMX3357` / `RE5469`) на процессоре **MediaTek Dimensity 1200 5G** (`MT6893`).  
Собрано под официальную прошивку **`RMX3357_13.1.0.500(CN01)`** (realme UI 4.0 / Android 13). Версия: **v2026.09.27**.

**Ключевые фичи:**
- Встроенный скрытый Root через KernelSU-Next и SuSFS 1.5.5 с аппаратным безопасным режимом.
- Убирает перегрев и жор батареи от стокового schedutil на Dimensity 1200.
- Держит стабильные 120 Гц без микростаттеров и троттлинга.
- Экономит заряд при прослушивании звука по Bluetooth и режет мусорную телеметрию MediaTek.
- Дополнительные сетевые, звуковые и диагностические возможности (WireGuard, BBR, CDC NCM, Bitperfect, SocketCAN, SDR, eBPF).

**Содержание:**
- [Быстрая установка и возврат на сток](#быстрая-установка-и-возврат-на-сток)
- [Скрытие и Root (KernelSU-Next + SuSFS 1.5.5)](#скрытие-и-root-kernelsu-next--susfs-155)
- [Энергоэффективность, плавность и термоконтроль](#энергоэффективность-плавность-и-термоконтроль)
- [Сеть, звук, накопитель, периферия и аппаратная совместимость](#сеть-звук-накопитель-периферия-и-аппаратная-совместимость)
- [Системные требования и совместимость](#системные-требования-и-совместимость)
- [Сборка из исходников](#сборка-из-исходников)
- [Источники и благодарности](#источники-и-благодарности)

---

### Быстрая установка и возврат на сток

> [!IMPORTANT]
> Перед прошивкой обязательно разблокируйте загрузчик и сохраните резервную копию стокового `boot.img`.

#### 1. Прошивка ядра через Fastboot
1. Переведите смартфон в режим загрузчика:
   ```bash
   adb reboot bootloader
   ```
2. Проверьте подключение устройства:
   ```bash
   fastboot devices
   ```
3. Прошейте образ ядра NeoCore:
   ```bash
   fastboot flash boot boot-rmx3357-neocore.img
   ```
4. Перезагрузитесь в систему:
   ```bash
   fastboot reboot
   ```

#### 2. Установка Root и модуля-компаньона
1. Установите [KernelSU-Next Manager APK (v3.4.0)](https://github.com/rifsxd/KernelSU-Next/releases).
2. Откройте KernelSU Manager (или Magisk) и прошейте архив модуля: **`rmx3357-neocore-companion.zip`** (исходники в каталоге [`userspace/`](userspace/)).
3. Перезагрузите смартфон. Модуль автоматически подхватит синхронизацию профилей ColorOS, перенесёт Bluetooth на малые ядра и отключит спам логгеров MediaTek.

#### 3. Аппаратный безопасный режим (Safe Mode)
Если установленный модуль мешает загрузке системы, нажмите клавишу уменьшения громкости (`Vol-`) три раза подряд во время включения смартфона. Ядро перехватит эту комбинацию и отключит загрузку модулей.

#### 4. Возврат на заводское ядро
Чтобы в любой момент вернуть стоковое ядро:
```bash
adb reboot bootloader
fastboot flash boot boot_stock.img
fastboot reboot
```

---

### Скрытие и Root (KernelSU-Next + SuSFS 1.5.5)

#### 1. Встроенный KernelSU-Next v3.4.0-legacy (UAPI 4)
- **Прямая интеграция в ядро**: Драйвер встроен непосредственно в исходный код ядра (`drivers/kernelsu`, `CONFIG_KSU_MANUAL_HOOK=y`), что исключает накладные расходы и сбои механизма динамических `kprobes`.
- **Изолированные дескрипторы UAPI v4**: Работает через защищённый протокол с файловыми дескрипторами `[ksu_driver_su]`, где root-доступ предоставляется только процессам с аутентифицированным дескриптором от менеджера.

#### 2. Скрытная подсистема SuSFS 1.5.5 на уровне VFS
- **Чистая архитектура VFS**: Стандартные пути VFS без грубых перенаправлений путей «на лету», что сохраняет блокировки POSIX и исключает повреждение баз данных SQLite.
- **Скрытие точек монтирования и дентри**: Вызовы `add_sus_mount` в `fs/namespace.c` вырезают скрытые точки из `/proc/mounts`, `/proc/mountinfo` и `/proc/mountstats`. Внутри критических секций RCU функция `__d_lookup_rcu()` в `fs/dcache.c` отсекает скрытые дентри с помощью `susfs_is_current_task_sus_blocked()`.
- **Подмена statfs для EROFS**: Функция `zeromount_spoof_statfs` для системных разделов (`/system`, `/vendor`, `/product`) подменяет системную статистику файловой системы (0 свободных блоков, сигнатура EROFS `0xe0f5e1e2`), устраняя аномалии детектирования read-only монтирований и оверлеев.
- **Маскировка памяти, дескрипторов и символов**: Подменяются пути модулей в `/proc/[pid]/maps`, срезаются дескрипторы inotify в `/proc/[pid]/fdinfo/`. Параметр `CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS` в `kernel/kallsyms.c` скрывает системные символы с префиксами `ksu_`, `susfs_` и `ksud`. Вызов `sys_newuname` в `kernel/sys.c` подменяет строки релиза под заводскую сертифицированную прошивку для успешного прохождения проверок Play Integrity.

#### 3. Интеграция с Hybrid Mount и NeoZygisk
- Полная совместимость со связкой [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) (`disable_umount = true`) для надёжной изоляции точек монтирования без взаимных блокировок и современным Zygisk-провайдером [NeoZygisk](https://github.com/JingMatrix/NeoZygisk).

#### 4. Аппаратный безопасный режим (Safe Mode)
- Если сторонний модуль вызывает бутлуп, трёхкратное нажатие клавиши уменьшения громкости (`KEY_VOLUMEDOWN` / `Vol-`) во время включения смартфона перехватывается драйвером ввода `drivers/input/input.c`, отключая загрузку всех модулей и возвращая систему в работоспособное состояние.

---

### Энергоэффективность, плавность и термоконтроль

#### 1. Профили энергоэффективности и частотные лимиты в schedutil
В стоковом ядре Dimensity 1200 планировщик `schedutil` агрессивно задирает производительные ядра Cortex-A78 до 2.6–3.0 ГГц даже при обычном скролле соцсетей или простых фоновых задачах. Процессор греется, потребляет до 3 Вт на пустом месте и быстро ловит жёсткий троттлинг. Штатный режим энергосбережения либо отключает ядра (создавая рывки из-за задержек хотплага), либо режет частоты слишком низко, ломая плавность 120 Гц.

В NeoCore лимиты частот зашиты прямо в выбор частоты планировщика (`get_next_freq()` в `kernel/sched/cpufreq_schedutil.c`). Чтение переменной идёт без блокировок через `smp_load_acquire`. Интерфейс остаётся отзывчивым на 120 Гц, но процессор больше не улетает в неэффективную верхнюю зону вольт-частотной кривой техпроцесса TSMC N6.

- **Аппаратный буст тача**: Драйвер `drivers/input/input.c` перехватывает аппаратные события касания `EV_ABS` и `BTN_TOUCH`, включая буст планировщика на 35 мс для мгновенного отклика.
- **Управление через терминал**: Переключать профили можно прямо из консоли под root:
  ```bash
  # Проверить активный профиль (0=Супер-энергосбережение, 1=Энергосбережение, 2=Баланс, 3=GT)
  cat /proc/sys/kernel/sched_power_profile

  # Включить профиль Баланс (рекомендуется для постоянного использования)
  su -c sysctl -w kernel.sched_power_profile=2

  # Включить режим супер-энергосбережения (максимальная экономия батареи)
  su -c sysctl -w kernel.sched_power_profile=0

  # Включить режим GT без ограничений частот
  su -c sysctl -w kernel.sched_power_profile=3
  ```

| ID | Профиль | Кластер Little (ядра 0–3) | Кластер Mid (ядра 4–6) | Prime-ядро (ядро 7) | Поведение и эффект |
| :---: | :--- | :---: | :---: | :---: | :--- |
| `0` | **Супер-энергосбережение** | 1.625 ГГц @ 906 мВ | Отключены (PPM) | 1.998 ГГц @ 875 мВ | Топология **4 Little + 1 Prime**. Средние ядра отключены, шина питания `VPROC2` обесточена. Запас GPU 10%, ток покоя 66 мА, пик 547 мВт. Экстремальное сохранение заряда. |
| `1` | **Энергосбережение** | 1.625 ГГц @ 906 мВ | 1.985 ГГц @ 919 мВ | 2.141 ГГц @ 900 мВ | Экономичный режим, активны все 8 ядер. Снижение энергопотребления на `-20.6%` по сравнению с Балансом, запас GPU 10%. Без микрофризов хотплага и с полной плавностью 120 Гц. |
| `2` | **Баланс** *(По умолчанию)* | 1.800 ГГц @ 950 мВ | 2.354 ГГц @ 1000 мВ | 2.463 ГГц @ 956 мВ | Физический оптимум чипа MT6893 на техпроцессе TSMC N6. Запас GPU 15% (`dvfs_margin_value=15`, `gx_fb_dvfs_margin=150`). Холодный корпус и стабильные 120 Гц без троттлинга. |
| `3` | **Режим GT (Без лимитов)** | 2.000 ГГц (Макс) | 2.600 ГГц (Макс) | 3.000 ГГц (Макс) | Полная мощность 8 ядер без ограничений для игр, тяжёлых вычислений и компиляции (пик 2979 мВт, запас GPU 15%). |

#### 2. Гармонический дисплейный таймер HZ (гармоника 120 Гц)
Стандартные ядра Android используют таймер с частотой 100, 250 или 300 Гц, которые не делятся нацело на длительность кадра 120 Гц (8.333 мс). В NeoCore задан гармонический квант `CONFIG_HZ=360` с периодом тика $1000 / 360 \approx 2.777\text{ мс}$:
- Ровно 3 тика укладываются в кадр 8.333 мс (120 Гц).
- Ровно 6 тиков укладываются в кадр 16.666 мс (60 Гц).  
Срабатывания планировщика чётко синхронизированы с границами кадров vblank дисплея, что исключает фазовый дрейф и микрорывки анимаций.

#### 3. Трёхуровневая изоляция Bluetooth LDAC на малых ядрах (0–3)
Передача звука с высоким битрейтом (LDAC 990 кбит/с) на стоковой прошивке вызывает быстрый разряд батареи, так как аудиопотоки и прерывания постоянно будят горячие ядра Cortex-A78:
- **Уровень 1 (Потоки кодека и аудиостека)**: Потоки процесса `com.android.bluetooth` (энкодер LDAC `bt_a2dp_source_`, `bt_stack_manage`, `BluetoothA2dpLo`, `bta_media_*`) переводятся в `/dev/cpuset/system-background/tasks` и фиксируются на ядрах 0–3 (`taskset -p 0f`).
- **Уровень 2 (Вендорный сервис HAL)**: Служба MediaTek Bluetooth Audio HAL (`android.hardware.bluetooth@1.1-service-mediatek`) переводится в `/dev/cpuset/system-background/cgroup.procs`, а все её рабочие потоки привязываются к ядрам 0–3 (`taskset -p 0f`).
- **Уровень 3 (Аппаратная маршрутизация прерываний ARM GICv3)**: Составная маска `0f` из-за механизма 1-of-N routing в контроллере GICv3 приводила к утечке до ~23.7% прерываний на ядра Cortex-A78. Мы развели прерывания побитовыми масками строго на малые ядра Cortex-A55:
  - IRQ 18 (`mtk btif tx dma irq`) -> Ядро 0 (`smp_affinity: 01`)
  - IRQ 19 (`mtk btif rx dma irq`) -> Ядро 1 (`smp_affinity: 02`)
  - IRQ 228 (`BTIF_WAKEUP_IRQ`) -> Ядро 2 (`smp_affinity: 04`)
  - IRQ 17 (`mtk btif irq`) и IRQ 281 (`BTCVSD_ISR_Handle`) -> Ядро 3 (`smp_affinity: 08`)
- **Результат**: На ядрах 4–7 регистрируется ровно 0 прерываний во время сна. Разряд батареи при прослушивании LDAC снизился в 4.5–5 раз (с 18–24%/ч до 3.5–5%/ч), малые ядра работают на базовых 600 МГц, а смартфон остаётся холодным (26.2 °C).

#### 4. Автосинхронизация профилей с ColorOS (`power_sync.sh`)
Асинхронный событийный демон на базе Toybox `inotifyd` с фильтром `:cy` (`close_write` / `moved_to`). Отслеживает изменения системных файлов в `/data/system/users/0` (`settings_system.xml`, `settings_global.xml`) и `/data/misc/bluetooth`:
- **Нулевой оверхед**: Демон не занимается периодическим опросом. В состоянии покоя он блокируется на вызове `read()`, потребляя ровно 0.0% процессора и не прерывая глубокий сон (Deep Sleep).
- **Синхронизация режимов ColorOS**:
  - «Суперэнергосбережение» (`super_powersave_mode_state=1`) -> Профиль `0`
  - «Энергосбережение» (`low_power=1`) -> Профиль `1`
  - «Сбалансированный» режим -> Профиль `2`
  - «Режим GT» (`gt_mode_state_setting=1`) или игровое пространство -> Профиль `3`
- **Динамическая настройка**: Автоматически регулирует порог запаса GED GPU DVFS (`dvfs_margin_value=10` или `15`) и обесточивает средние ядра через MediaTek PPM в режиме супер-энергосбережения.

#### 5. Очистка отладочного спама MediaTek и ColorOS
Скрипт `service.sh` при старте глушит фоновые логгеры и службы отправки дампов:
- Останавливаются: `emdlogger`, `connsyslogger`, `mobile_log_d`, `bt_dump`, `wifi_dump`, `netdiag`, `criticallog`, `common_dcs`, `phoenix_log_manager`.
- Служба `oiface` остаётся включённой, чтобы игровой стабилизатор фреймрейта и GPU DVFS работали штатно.

---

### Сеть, звук, накопитель, периферия и аппаратная совместимость

#### 1. Высокоскоростной тетеринг по USB через CDC NCM
- **Тетеринг по USB через CDC NCM**: Устаревший протокол RNDIS заменён современным CDC NCM (`CONFIG_USB_CONFIGFS_NCM=y`, `CONFIG_USB_NET_CDC_NCM=y`). Это снижает нагрузку на процессор и повышает скорость передачи данных при раздаче интернета по USB.
- **Исправление сброса NTB-буфера**: Мультипакетные гаджеты CDC NCM (`f_ncm.c`) сбрасывают накопленный блок NTB вызовом `ndo_start_xmit(NULL, dev)`. Стоковый код ядра `drivers/usb/gadget/function/u_ether.c` отклонял пакеты `skb == NULL` с ошибкой `-EINVAL`. Разрешение нулевых пакетов и перенос указателя `pinfo` за пределы `dev->wrap()` наладили сброс буфера и устранили зависания передачи под гигабитным трафиком.
- **Совместимость со стоковыми скриптами**: В `f_ncm.c` зарегистрирован псевдоним `rndis`, благодаря чему штатные скрипты `init.rc` поднимают сеть без правок userspace.

#### 2. Сетевой стек, буферблоат, WireGuard, TPROXY и SMB3
- **Борьба с bufferbloat**: Основной дисциплиной очередей по умолчанию выбран **FQ-CoDel** (`CONFIG_DEFAULT_NET_SCH="fq_codel"`), удерживающий низкую задержку при нагрузке. Также в ядро вкомпилированы алгоритмы контроля перегрузки Google BBR v1 (`CONFIG_TCP_CONG_BBR=y`), Westwood+ и планировщик CAKE.
- **Прозрачные прокси и обход лимитов раздачи**: Таргет `CONFIG_NETFILTER_XT_TARGET_TPROXY=y` обеспечивает прозрачный перехват сокетов для клиентов Sing-box, Clash и Xray. Таргеты `TTL` и `HL` в Netfilter позволяют зафиксировать значение TTL для обхода ограничений мобильных операторов:
  ```bash
  # Фиксация исходящего TTL IPv4 на значении 64
  su -c iptables -t mangle -A POSTROUTING -j TTL --ttl-set 64
  # Фиксация исходящего Hop Limit IPv6 на значении 64
  su -c ip6tables -t mangle -A POSTROUTING -j HL --hl-set 64
  ```
- **Встроенный WireGuard и контейнеры**: Поддержка WireGuard прямо в ядре (`CONFIG_WIREGUARD=y`), а также пространства имён (`USER_NS`, `PID_NS`, `NET_NS`), cgroups, `veth` и сетевой мост bridge для запуска Linux-контейнеров.
- **Монтирование сетевых дисков SMB3**: Прямое подключение сетевых хранилищ SMB3/CIFS на уровне ядра (`CONFIG_CIFS=y`, `CONFIG_NETWORK_FILESYSTEMS=y`). Модуль NFS отключён (`CONFIG_NFS_FS=n`), чтобы не ломать совместимость с Google VINTF на Android 13:
  ```bash
  su -c mkdir -p /mnt/share
  su -c mount -t cifs //192.168.1.100/Data /mnt/share \
      -o username=user,password=pass,vers=3.0,iocharset=utf8
  ```
- **Сетевой стек TCP**: Задействован алгоритм Cubic (`tcp_congestion_control=cubic`), включён TCP Fast Open (`tcp_fastopen=3`), установлен надёжный режим ответа ECN (`tcp_ecn=2`) для защиты от сброса соединений мобильными провайдерами, отключён сброс окна после простоя.

#### 3. Звук Bitperfect, режим USB ЦАП, ALSA Loopback и MIDI
- **Bitperfect USB Audio**: Поддержка протоколов UAC1/UAC2 (`CONFIG_SND_USB_AUDIO=y`) с асинхронными эндпоинтами и прямым выводом DSD/DoP без передискретизации системным аудиомикшером Android.
- **Режим USB ЦАП**: Смартфон можно использовать в роли внешней звуковой карты для компьютера (`CONFIG_USB_CONFIGFS_F_UAC2=y`).
- **ALSA Loopback и MIDI**: Виртуальная звуковая петля (`CONFIG_SND_ALOOP=m`) и интерфейс USB MIDI (`CONFIG_SND_RAWMIDI=y`, `CONFIG_USB_CONFIGFS_F_MIDI=y`).

#### 4. Периферия и диагностика: SocketCAN, SDR, UART и Wi-Fi 6
- **Шина SocketCAN для автодиагностики**: Поддержка CAN bus (`slcan`, `vcan`, `can_raw`) для подключения к диагностическому разъёму автомобиля OBD-II:
  ```bash
  # Запуск адаптера USB-CAN на скорости 500 кбит/с (стандарт OBD-II)
  su -c slcand -o -c -s6 /dev/ttyUSB0 slcan0
  su -c ip link set slcan0 up
  su -c candump slcan0
  ```
- **Программно-определяемое радио (SDR)**: Драйвер чипа Realtek RTL2832U DVB-T (`CONFIG_DVB_USB_RTL28XXU=m`) для приёма радиосигнала и телеметрии.
- **Последовательные порты UART**: В ядро вшиты драйверы переходников USB-UART на чипах CH340/CH341, CP2102/CP2104, FTDI FT232 и PL2303.
- **Аудит беспроводных сетей (Wi-Fi 6 / mac80211)**: Стек `mac80211` (`CONFIG_MAC80211=m`) с поддержкой режима монитора и пакетной инъекции на внешних Wi-Fi адаптерах (Atheros AR9271, Ralink RT3070, Realtek RTL8187).

#### 5. Оптимизация накопителя UFS 3.1, очередей и кучи ART Dalvik VM
- **Очереди блочных устройств UFS 3.1**: Накопители `sd*` настраиваются при загрузке (планировщик `cfq`, `nr_requests=256`, `read_ahead_kb=256`, `rq_affinity=1`, `nomerges=0`, `add_random=0`).
- **Разблокировка многоочередных элеваторов**: В коде ядра (`block/elevator.c`) снят вендорный запрет в `elevator_init_mq()`, открывая доступ к многоочередным планировщикам `mq-deadline` и `kyber` вместо принудительного `none`.
- **Границы кучи ART Dalvik VM**: В файле `system.prop` задаются размеры кучи через `resetprop` до старта Zygote (`heapstartsize=12m`, `heapgrowthlimit=512m`, `heapsize=768m`), что предотвращает задержки сборщика мусора и выгрузку приложений из памяти.
- **Изоляция служб мониторинга и модема**: Демоны датчиков здоровья, термопар и питания принудительно переведены на малые ядра, чтобы не будить кластер A78. Процесс сотового модема `mtkfusionrild` получил мягкий приоритет малых ядер, при этом высокоскоростная маршрутизация 4G/5G данных обрабатывается ядром и DSP модема без снижения скорости.

#### 6. Совместимость с закрытыми драйверами вендора (инварианты MediaTek KABI)
- **Точные смещения структур KABI**: Расположение полей в структурах `net_device` (`dev_addr` строго со смещением **752** байта / `0x2f0`) и `task_struct` (`thread` строго со смещением **3168** байт, защищено `BUILD_BUG_ON`) сохранено байт в байт, что исключает сбои и панику ядра в проприетарных бинарных модулях MediaTek.
- **Терпимость к расхождению версий CRC**: Функция `check_version()` в `kernel/module.c` при несовпадении контрольных сумм символов устанавливает флаг `TAINT_FORCED_MODULE` и разрешает загрузку модуля вместо завершения с ошибкой `-ENOEXEC`. В `module_sig_check()` ослаблена проверка подписей для загрузки закрытых вендорных модулей.
- **Экспорт символа Wi-Fi 6**: В файл `wmt_build_in_adapter.c` добавлен экспорт функции `conn_dbg_add_log`. Без него закрытый драйвер Wi-Fi 6 `wlan_drv_gen4m.ko` не может слинковаться и падает при запуске.
- **Защита SELinux от NULL-указателей**: Функции `selinux_inode()` и `selinux_cred()` в `security/selinux/include/objsec.h` проверяют указатели на NULL перед чтением структур, предотвращая падение ядра при проверке прав на синтетических файлах OverlayFS, KernelSU или SuSFS.

#### 7. Подсистема eBPF, аппаратный JIT и трассировка ядра
- **Аппаратный BPF JIT**: Нативный JIT-компилятор под AArch64 (`CONFIG_BPF_JIT=y`, `arch/arm64/net/bpf_jit_comp.c`) с полным отключением интерпретатора (`CONFIG_BPF_JIT_ALWAYS_ON=y`) для максимального быстродействия и защиты от атак класса Spectre v2.
- **Учёт трафика Android и cgroups**: Опции `CONFIG_CGROUP_BPF=y`, `CONFIG_NET_CLS_BPF=y` и `CONFIG_NETFILTER_XT_MATCH_BPF=y` обеспечивают per-UID мониторинг трафика в Android `netd`, классификацию пакетов и системные ограничения передачи данных.
- **Динамическая трассировка и пробы**: Поддержка kprobes, uprobes и perf events (`CONFIG_BPF_EVENTS=y`, `CONFIG_KPROBE_EVENTS=y`, `CONFIG_UPROBE_EVENTS=y`) позволяет подключать eBPF-программы для профилирования задержек, анализа подсистемы ввода-вывода и телеметрии планировщика.
- **Границы архитектуры 4.19**: В ядре 4.19 рут-контроль изолирован через ручные VFS-хуки (`CONFIG_KSU_MANUAL_HOOK=y`). Технологии BPF LSM и vmlinux BTF (CO-RE) намеренно не переносились из ветки 5.x для сохранения стабильности бинарного KABI MediaTek и соблюдения 32 МБ лимита boot-раздела.

---

### Системные требования и совместимость

- **Устройство**: Realme GT Neo 2T (`RMX3357` / `RE5469`).
- **Платформа**: MediaTek Dimensity 1200 5G (`MT6893`).
- **Базовая прошивка**: Официальная realme UI 4.0 / ColorOS 13.1 на Android 13 (`RMX3357_13.1.0.500(CN01)`).
- **Загрузчик**: Требуется разблокированный загрузчик (Bootloader Unlocked).

---

### Сборка из исходников

- **Инструменты сборки**: AOSP Clang 12.0.5 (`clang-r416183b`), LLD 12.0.5, GNU binutils 2.34 (`aarch64-linux-gnu-`, `arm-linux-gnueabi-`).
- **Базовый дефконфиг**: `arch/arm64/configs/k6893v1_64_k419_defconfig` (или `k6893v1_64_k419_ab_defconfig` для A/B).
- **Пользовательский оверлей**: `arch/arm64/configs/custom_rmx3357.config`.

```bash
export ARCH=arm64
export SUBARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export CROSS_COMPILE_ARM32=arm-linux-gnueabi-
export CC=clang
export CLANG_TRIPLE=aarch64-linux-gnu-

# Создание базовой конфигурации
make k6893v1_64_k419_defconfig

# Применение оверлея с настройками NeoCore
cat arch/arm64/configs/custom_rmx3357.config >> .config
make olddefconfig

# Компиляция ядра и модулей
make -j$(nproc) Image.gz modules
```

---

### Источники и благодарности

- [Realme Open Source Kernel Base](https://github.com/realme-kernel-opensource/realme_X7max_GTneo_GTneo2T-AndroidT-kernel-source)
- [Realme Open Source Vendor Base](https://github.com/realme-kernel-opensource/realme_X7max_GTneo_GTneo2T-AndroidT-vendor-source)
- [KernelSU-Next](https://github.com/rifsxd/KernelSU-Next) — [@rifsxd](https://github.com/rifsxd)
- [SuSFS](https://gitlab.com/simonpunk/susfs4ksu) — [@simonpunk](https://gitlab.com/simonpunk) & [@sidex102](https://github.com/sidex102)
- [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) — [Hybrid-Mount](https://github.com/Hybrid-Mount)
- [NeoZygisk](https://github.com/JingMatrix/NeoZygisk) — [@JingMatrix](https://github.com/JingMatrix)
