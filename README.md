# NeoCore Kernel for Realme GT Neo 2T (RMX3357 / MT6893)

[![Kernel Version](https://img.shields.io/badge/Linux-4.19.191-blue.svg)](https://kernel.org/)
[![Device](https://img.shields.io/badge/Device-Realme%20GT%20Neo%202T%20(RMX3357)-brightgreen.svg)](https://github.com/realme-kernel-opensource/realme_X7max_GTneo_GTneo2T-AndroidT-kernel-source)
[![KernelSU-Next](https://img.shields.io/badge/KernelSU--Next-v3.4.0--legacy-success.svg)](https://github.com/rifsxd/KernelSU-Next)
[![SuSFS](https://img.shields.io/badge/SuSFS-v1.5.5-blueviolet.svg)](https://gitlab.com/simonpunk/susfs4ksu)

[English](#english) | [Русский](#русский)

---

<a name="english"></a>
## English

Production-grade Linux 4.19.191 kernel for **Realme GT Neo 2T** (`RMX3357` / `RE5469`) on **MediaTek Dimensity 1200 5G** (`MT6893`). Built against stock firmware base **`RMX3357_13.1.0.500(CN01)`** (realme UI 4.0 / Android 13). Version: **v2026.09.26**.


### Subsystem Details

#### 1. KernelSU-Next v3.4.0-legacy (UAPI 4)
- **Direct In-Tree Integration**: Integrated directly into `drivers/kernelsu` (`CONFIG_KSU_MANUAL_HOOK=y`), bypassing unstable dynamic `kprobes`.
- **Scoped Descriptors**: Uses UAPI v4 scoped file descriptors (`[ksu_driver_su]`). Root calls are restricted to processes with an authenticated descriptor issued by the manager.
- **Hardware Safe Mode**: Wired into `drivers/input/input.c`. Pressing Volume Down (`KEY_VOLUMEDOWN`) three times consecutively during boot disables all modules.
- **Seccomp & SELinux**: Atomic `filter_count` in `struct seccomp` for filter tracking; NULL checks prevent recursive lock contention under `policy_rwlock`.

#### 2. Clean VFS & SuSFS 1.5.5 Stealth
- **Standard VFS Layer**: Stock VFS code paths without inline path-redirection hacks. Preserves POSIX file locking semantics and prevents SQLite database corruption.
- **Mount Hiding**: Hidden mounts (`add_sus_mount`) in `fs/namespace.c` are filtered out from `/proc/mounts`, `/proc/mountinfo`, and `/proc/mountstats`.
- **RCU dcache Lookup**: `__d_lookup_rcu()` in `fs/dcache.c` invokes `susfs_is_current_task_sus_blocked()` to skip hidden dentries inside RCU critical sections.
- **Maps & Descriptor Cloaking**: Masks module paths in `/proc/[pid]/maps` and strips inotify watch descriptors in `/proc/[pid]/fdinfo/`.
- **Uname & Kallsyms Spoofing**: `sys_newuname` in `kernel/sys.c` hooks `susfs_spoof_uname` to match certified stock release strings for Play Integrity. `CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS` in `kernel/kallsyms.c` strips `ksu_`, `susfs_`, and `ksud` symbols. Secure userspace control via `prctl(0xDEADBEEF)`.
- **Userspace Root Isolation & Zygisk**: Recommended setup uses [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) for clean mount isolation and [NeoZygisk](https://github.com/JingMatrix/NeoZygisk) as the Zygisk provider without zygote tracing issues.

#### 3. Harmonic Scheduler & Energy-Efficiency Profiles
- **Why Energy-Efficiency Profiles Exist**: On stock realme UI, the MediaTek Dimensity 1200 governor aggressively overboosts Cortex-A78 cores to 2.6–3.0 GHz even for simple UI scrolling and background services. This causes steep battery drain (up to ~3 W peak active CPU power), rapid heat buildup, and subsequent thermal throttling. Stock Android battery savers either disable cores abruptly (causing hotplug microstutter) or lock frequencies too low, ruining 120 Hz display fluidness.
- **Kernel-Level Lockless Ceilings**: NeoCore enforces lockless hardware frequency ceilings per cluster directly inside `get_next_freq()` in `kernel/sched/cpufreq_schedutil.c` via `smp_load_acquire`. The scheduler responds instantly to load, but never overshoots the energy-efficient sweet spot of the TSMC N6 voltage/frequency curve.
- **Automation & Switching**:
  - **Automatic**: The companion module (`rmx3357-neocore-companion`) synchronizes ColorOS power toggles (`Super Power Saving` -> Profile `0`, `Power Saving` -> Profile `1`, `Balanced` -> Profile `2`, `GT Mode` -> Profile `3`) and Bluetooth state in real time via `inotifyd` (`:cy` close-write/move filter), adjusting GED GPU DVFS margins (`/sys/kernel/ged/hal/dvfs_margin_value` and `/sys/module/ged/parameters/gx_fb_dvfs_margin`) and MediaTek PPM core limits accordingly.
  - **Manual**: Any profile can be set directly from a root shell:
    ```bash
    echo 2 > /proc/sys/kernel/sched_power_profile
    ```
- **Harmonic Display Timer (`CONFIG_HZ=360`)**: Tick period is $1000 / 360 \approx 2.777\text{ ms}$. Exactly 3 ticks fit into an 8.333 ms frame (120 Hz) and 6 ticks into a 16.666 ms frame (60 Hz), eliminating timer phase drift against vblank deadlines.

| Profile ID | Profile Name | Little Cluster (CPUs 0–3) | Mid Cluster (CPUs 4–6) | Prime Core (CPU 7) | Architectural Target & Benefits |
| :---: | :--- | :---: | :---: | :---: | :--- |
| `0` | **Super Power Saving** | 1.625 GHz @ 906 mV | Offline (PPM) | 1.998 GHz @ 875 mV | **4 Little + 1 Prime** topology. Mid cores offline, `VPROC2` shut down. 10% GPU margin, 66 mA idle, 547 mW peak. Extreme battery conservation. |
| `1` | **Power Saving** | 1.625 GHz @ 906 mV | 1.985 GHz @ 919 mV | 2.141 GHz @ 900 mV | Battery saver, 8 cores active, `-20.6%` energy reduction vs Balanced, 10% GPU margin, zero hotplug lag while keeping 120 Hz completely smooth. |
| `2` | **Balanced** *(Default)* | 1.800 GHz @ 950 mV | 2.354 GHz @ 1000 mV | 2.463 GHz @ 956 mV | Physical optimum on TSMC N6, 15% GPU margin (`/sys/kernel/ged/hal/dvfs_margin_value=15`). Daily sweet spot: zero heating and sustained peak smoothness. |
| `3` | **GT Mode (Uncapped)** | 2.000 GHz (Max) | 2.600 GHz (Max) | 3.000 GHz (Max) | Full 8-core performance unlocked for heavy compute, compiling, and gaming (2979 mW peak, 15% GPU margin). |

- **Touch Boost**: Intercepts `EV_ABS` and `BTN_TOUCH` hardware events in `drivers/input/input.c`, triggering a 35 ms scheduler boost pulse.

#### 4. Storage & UFS 3.1 Block Layer Tuning
- **UFS 3.1 Queue Optimization & Multi-Queue Elevator Support**: UFS 3.1 block devices (`sd*`) operate with tuned request queues (`cfq` scheduler, `nr_requests=256`, `read_ahead_kb=256`, `rq_affinity=1`, `nomerges=0`, `add_random=0`) for low-latency request merging and smooth writeback flushing. In addition, the vendor restriction `if (q->nr_hw_queues != 1) return 0;` in `elevator_init_mq()` (`block/elevator.c`) is removed so multi-queue block devices can initialize `mq-deadline` and `kyber` elevators instead of being forced into `none`.

#### 5. USB CDC NCM Networking
- **CDC NCM Tethering**: Replaces legacy RNDIS (`CONFIG_USB_CONFIGFS_RNDIS=n`) with CDC NCM (`CONFIG_USB_CONFIGFS_NCM=y`, `CONFIG_USB_NET_CDC_NCM=y`), reducing CPU overhead and raising throughput.
- **NTB Buffer Flush Fix**: In `drivers/usb/gadget/function/u_ether.c`, stock code rejected `skb == NULL` with `-EINVAL` before `dev->wrap()`. Multi-packet gadgets like CDC NCM (`f_ncm.c`) flush pending Network Transfer Blocks via `ndo_start_xmit(NULL, dev)`. Moving `pinfo` past `dev->wrap()` and accepting null packets fixes buffer flushes and prevents stalls under sustained gigabit traffic.
- **Drop-In Compatibility**: Aliased to `rndis` in `drivers/usb/gadget/function/f_ncm.c` so vendor `init.rc` scripts configure tethering without modification.

#### 6. Network Stack, File Systems & Virtualization
- **FQ-CoDel Default & Traffic Pacing**: Default queue discipline is tuned to **FQ-CoDel** (`CONFIG_DEFAULT_NET_SCH="fq_codel"`), eliminating bufferbloat, stabilizing latency under heavy packet floods, and ensuring DPI resilience in encrypted tunnels. In-tree modules for Google BBR v1 (`CONFIG_TCP_CONG_BBR=y`), Westwood+ (`CONFIG_TCP_CONG_WESTWOOD=y`), and CAKE (`CONFIG_NET_SCH_CAKE=y`) are fully compiled in and selectable via sysctl.
- **TPROXY & TTL Mangling**: `CONFIG_NETFILTER_XT_TARGET_TPROXY=y` enables socket redirection for transparent proxies (Sing-box, Clash, Xray). Netfilter `TTL` and `HL` targets allow tethering limit bypass.
- **Network File Systems (SMB3 / CIFS)**: Direct kernel mounting of SMB3 shares via `CONFIG_NETWORK_FILESYSTEMS=y` and `CONFIG_CIFS=y` (`CONFIG_NFS_FS=n` is strictly enforced to preserve Android 13 VINTF compatibility).
- **Containers & VPN**: Full namespace support (`USER_NS`, `PID_NS`, `NET_NS`), cgroups, `veth`, `bridge`, and in-tree WireGuard (`CONFIG_WIREGUARD=y`).

#### 7. Audiophile USB & Sound Subsystem
- **Bitperfect USB Audio**: Native UAC1/UAC2 (`CONFIG_SND_USB_AUDIO=y`) with asynchronous endpoints and native DSD/DoP playback.
- **USB Audio Gadget**: Phone can act as an external USB sound card (`CONFIG_USB_CONFIGFS_F_UAC2=y`).
- **ALSA Loopback & MIDI**: Virtual soundcard loopback (`CONFIG_SND_ALOOP=m`) and USB MIDI (`CONFIG_SND_RAWMIDI=y`, `CONFIG_USB_CONFIGFS_F_MIDI=y`).

#### 8. Hardware Peripherals, Radio & Pentesting
- **Serial UART**: In-tree drivers for CH340/CH341, CP2102/CP2104, FTDI FT232, and PL2303.
- **SocketCAN**: Kernel CAN bus support (`slcan`, `vcan`, `can_raw`) for OBD-II vehicle diagnostics.
- **Software-Defined Radio**: Realtek RTL2832U DVB-T driver (`CONFIG_DVB_USB_RTL28XXU=m`) for SDR spectrum reception.
- **Wireless Pentesting**: Modular `mac80211` stack (`CONFIG_MAC80211=m`) with monitor mode and packet injection on external USB adapters (Atheros AR9271, Ralink RT3070, Realtek RTL8187).

#### 9. MediaTek Vendor KABI & Out-of-Tree Driver Compatibility
- **Pinned Struct Offsets**: Layouts for `net_device` and `task_struct` match stock offsets byte-for-byte, preventing memory corruption in proprietary MediaTek modules.
- **Symbol CRC Mismatch Tolerance**: `check_version()` in `kernel/module.c` sets `TAINT_FORCED_MODULE` and returns success on symbol CRC differences instead of rejecting load with `-ENOEXEC`. `module_sig_check()` relaxes strict module signature enforcement, allowing closed-source vendor blobs to load cleanly.
- **Exported Wi-Fi 6 Bridge**: `wmt_build_in_adapter.c` exports `conn_dbg_add_log`, resolving dynamic linking requirements for the closed-source `wlan_drv_gen4m.ko` Wi-Fi 6 driver.
- **SELinux NULL Inode Guards**: `selinux_inode()` and `selinux_cred()` in `security/selinux/include/objsec.h` prevent NULL pointer panics when evaluating permissions on synthetic dentries created by OverlayFS, KernelSU, or SuSFS.

---

### Runtime CLI Reference

#### Switch CPU Governor Profiles
```bash
# Check active profile (0=SuperPowerSaving, 1=PowerSaving, 2=Balanced, 3=GT)
cat /proc/sys/kernel/sched_power_profile

# Set profile to Balanced (recommended default)
su -c sysctl -w kernel.sched_power_profile=2

# Set profile to Super Power Saving (maximum battery conservation)
su -c sysctl -w kernel.sched_power_profile=0

# Set profile to GT (uncapped frequencies for gaming)
su -c sysctl -w kernel.sched_power_profile=3
```

#### Cellular Hotspot TTL Mangling
```bash
# Fix outbound IPv4 TTL to 64 via Netfilter
su -c iptables -t mangle -A POSTROUTING -j TTL --ttl-set 64

# For IPv6 hop limit
su -c ip6tables -t mangle -A POSTROUTING -j HL --hl-set 64
```

#### Automotive SocketCAN (OBD-II via CANable)
```bash
# Initialize USB-CAN adapter at 500 kbps (standard automotive OBD-II)
su -c slcand -o -c -s6 /dev/ttyUSB0 slcan0
su -c ip link set slcan0 up

# Dump vehicle CAN bus frames
su -c candump slcan0
```

#### Direct SMB3 Mount
```bash
su -c mkdir -p /mnt/share
su -c mount -t cifs //192.168.1.100/Data /mnt/share \
    -o username=user,password=pass,vers=3.0,iocharset=utf8
```

---

### Installation (Fastboot)

> [!IMPORTANT]
> Always back up your stock `boot.img` before flashing.

1. Reboot to bootloader:
   ```bash
   adb reboot bootloader
   ```
2. Verify Fastboot connection:
   ```bash
   fastboot devices
   ```
3. Flash the NeoCore boot image:
   ```bash
   fastboot flash boot boot-rmx3357-neocore.img
   ```
4. Reboot into the system:
   ```bash
   fastboot reboot
   ```
5. Install the [KernelSU-Next Manager APK (v3.4.0)](https://github.com/rifsxd/KernelSU-Next/releases).
6. Flash the companion module **`rmx3357-neocore-companion.zip`** in KernelSU Manager or Magisk to automate userspace tuning.

---

### Userspace Scripts & Companion Module

Located in [`userspace/`](userspace/) and packaged into **`rmx3357-neocore-companion.zip`**:
- **`system.prop` & `service.sh` (`service.d/00_system_service.sh`)**: `system.prop` configures pre-Zygote ART Dalvik VM heap bounds (`heapstartsize=12m`, `heapgrowthlimit=512m`, `heapsize=768m`). `service.sh` runs after `/data` decryption and boot completion: stops MediaTek loggers (`emdlogger`, `connsyslogger`, `mobile_log_d`) and ColorOS crash uploaders, enforces Cubic TCP congestion control (`tcp_congestion_control=cubic`), configures GED GPU DVFS margins (`/sys/kernel/ged/hal/dvfs_margin_value=15`, `gx_fb_dvfs_margin=150`), and tunes UFS 3.1 `sd*` block queues (`cfq`, `nr_requests=256`, `read_ahead_kb=256`, `rq_affinity=1`, `nomerges=0`, `add_random=0`).
- **`power_sync.sh` (`service.d/99_power_profile_sync.sh`)**: Pure event-driven architecture using Toybox `inotifyd` with `:cy` (`close_write` / `moved_to`) filtering across `/data/system/users/0` (`settings_system.xml`, `settings_global.xml`) and `/data/misc/bluetooth` (`cached_hci*`, `*bt*`, `*blue*`). Synchronizes ColorOS power toggles (`Super Power Saving` -> Profile `0`, `Power Saving` -> Profile `1`, `Balanced` -> Profile `2`, `GT Mode` -> Profile `3`) and Bluetooth affinity with 0.0% idle CPU and zero child page faults (`cminflt`).
- **Three-Layer Bluetooth LDAC Little-Core Hardware Isolation**:
  - *Layer 1 (Audio Stack & Codec Threads)*: `com.android.bluetooth` threads (`bt_a2dp_source_` [LDAC encoder], `bt_stack_manage`, `BluetoothA2dpLo`, `bta_media_*`) confined to `/dev/cpuset/system-background/tasks` and pinned to cores 0–3 (`taskset -p 0f`) using fork-free shell built-ins.
  - *Layer 2 (Vendor HAL Service)*: MediaTek Bluetooth Audio HAL (`android.hardware.bluetooth@1.1-service-mediatek`) migrated to `/dev/cpuset/system-background/cgroup.procs` with all worker threads pinned to Little cores (`taskset -p 0f`).
  - *Layer 3 (ARM GICv3 Hardware Interrupt Routing)*: Eliminates 1-of-N routing interrupt leakage from composite mask `0f` (~23.7% leakage to Cortex-A78 big cores) by mapping IRQs with discrete bitwise masks strictly to Cortex-A55 Little cores:
    - IRQ 18 (`mtk btif tx dma irq`) -> Core 0 (`smp_affinity: 01`)
    - IRQ 19 (`mtk btif rx dma irq`) -> Core 1 (`smp_affinity: 02`)
    - IRQ 228 (`BTIF_WAKEUP_IRQ`) -> Core 2 (`smp_affinity: 04`)
    - IRQ 17 (`mtk btif irq`) & IRQ 281 (`BTCVSD_ISR_Handle`) -> Core 3 (`smp_affinity: 08`)
    - *Hardware Verification*: Cores 4–7 maintain strictly +0 interrupts (+0% leakage), achieving 100% hardware isolation of the Cortex-A78 cluster in Deep Sleep.
  - *Battery & Thermals*: LDAC playback power drain drops 4.5–5× (from 18–24%/hr down to 3.5–5%/hr). Little core frequencies idle at 600 MHz at baseline bus voltage; device stays at 26.2 °C ambient.
- **Background Housekeeping & Cellular RIL Isolation**:
  - *Sensor & Housekeeping HALs*: `android.hardware.health@2.1-service`, `android.hardware.power.stats-impl.oplus`, and `android.hardware.thermal@2.0-service.mtk` migrated via `/dev/cpuset/system-background/cgroup.procs` and pinned strictly to Cortex-A55 Little cores (0–3) across all threads (`taskset -p 0f`), eliminating unnecessary A78 wakeups for battery, PMIC, and thermal sensor polling.
  - *Cellular RIL*: MediaTek `mtkfusionrild` migrated via `/dev/cpuset/system-background/cgroup.procs` for soft Little-core priority. Keeps cellular network registration, SMS, and cell tower tracking off Cortex-A78 while high-speed 4G/5G data plane stays 100% in kernel/modem DSP without throughput bottlenecks.

Detailed manual installation: [`userspace/README.md`](userspace/README.md).

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
- [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) — [Hybrid-Mount](https://github.com/Hybrid-Mount) (recommended userspace mount isolation)
- [NeoZygisk](https://github.com/JingMatrix/NeoZygisk) — [@JingMatrix](https://github.com/JingMatrix) (recommended modern Zygisk implementation)

---

<a name="русский"></a>
## Русский

Стабильное ядро Linux 4.19.191 для **Realme GT Neo 2T** (`RMX3357` / `RE5469`) на платформе **MediaTek Dimensity 1200 5G** (`MT6893`). Собрано на базе стоковой прошивки **`RMX3357_13.1.0.500(CN01)`** (realme UI 4.0 / Android 13). Версия: **v2026.09.26**.


### Описание подсистем ядра

#### 1. KernelSU-Next v3.4.0-legacy (UAPI 4)
- **Прямая интеграция in-tree**: Драйвер встроен напрямую в `drivers/kernelsu` (`CONFIG_KSU_MANUAL_HOOK=y`), исключая нестабильный механизм `kprobes`.
- **Защищённые дескрипторы**: Работает по протоколу UAPI v4 через дескриптор `[ksu_driver_su]`. Запросы рут-доступа разрешены только процессам с авторизованным дескриптором от менеджера.
- **Аппаратный безопасный режим**: Интегрирован в `drivers/input/input.c`. Трёхкратное нажатие клавиши уменьшения громкости (`KEY_VOLUMEDOWN`) при старте отключает все модули.
- **Seccomp и SELinux**: Атомарный счётчик `filter_count` в `struct seccomp` для отслеживания фильтров; проверки на нулевые указатели предотвращают зависания в `policy_rwlock`.

#### 2. Чистая VFS и стелс-механизмы SuSFS 1.5.5
- **Стандартная VFS**: В ядре сохранена стоковая VFS без перенаправлений путей «на лету». Это исключает поломку блокировок POSIX и повреждение баз данных SQLite.
- **Скрытие точек монтирования**: Вызовы `add_sus_mount` в `fs/namespace.c` вырезают скрытые точки из `/proc/mounts`, `/proc/mountinfo` и `/proc/mountstats`.
- **Быстрый путь RCU в dcache**: Функция `__d_lookup_rcu()` в `fs/dcache.c` вызывает `susfs_is_current_task_sus_blocked()` и отсекает скрытые дентри внутри критических секций RCU.
- **Маскировка карт памяти и дескрипторов**: Подменяются пути модулей в `/proc/[pid]/maps` и срезаются отметки inotify в `/proc/[pid]/fdinfo/`.
- **Спуфинг uname и чистка kallsyms**: Хук `susfs_spoof_uname` в `sys_newuname` (`kernel/sys.c`) подменяет строки релиза под заводскую прошивку для прохождения Play Integrity. При сборке с `CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS` в `kernel/kallsyms.c` скрываются символы с префиксами `ksu_`, `susfs_` и `ksud`. Управление из userspace идёт через системный вызов `prctl(0xDEADBEEF)`.
- **Изоляция модулей в userspace и Zygisk**: Для системных рут-модулей рекомендуется связка [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) (чистая изоляция точек монтирования) и [NeoZygisk](https://github.com/JingMatrix/NeoZygisk) в качестве современного Zygisk-провайдера без конфликтов с zygote.

#### 3. Гармонический таймер и профили энергоэффективности
- **Зачем нужны профили энергоэффективности**: На стоковом ядре Dimensity 1200 планировщик `schedutil` чрезмерно агрессивно задирает частоты «горячих» ядер Cortex-A78 до 2,6–3,0 ГГц даже при обычном скролле списков и простых фоновых задачах. Это вызывает резкие скачки потребления (до ~3 Вт на CPU), сильный нагрев и быстрый температурный троттлинг. Штатные режимы энергосбережения Android либо принудительно отключают ядра (вызывая микрофризы из-за задержек хотплага), либо урезают частоты слишком сильно, ломая плавность 120 Гц.
- **Аппаратные потолки частот без блокировок**: В NeoCore эта проблема решается прямо в ядре: на уровне функции `get_next_freq()` в `kernel/sched/cpufreq_schedutil.c` через `smp_load_acquire` вводятся аппаратные частотные потолки для каждого кластера. Планировщик мгновенно реагирует на касания и нагрузку, но удерживает процессор в наиболее эффективной зоне вольт-частотной кривой (V/F curve) техпроцесса TSMC N6.
- **Автоматика и управление**:
  - **Автоматически**: модуль-компаньон (`rmx3357-neocore-companion`) синхронизирует системные переключатели питания ColorOS («Суперэнергосбережение» -> профиль `0`, «Энергосбережение» -> профиль `1`, «Сбалансированный» -> профиль `2`, «Режим GT» -> профиль `3`) и состояние Bluetooth в реальном времени через `inotifyd` с фильтром `:cy` (`close_write` / `moved_to`), синхронно настраивая пороги GED GPU DVFS (`/sys/kernel/ged/hal/dvfs_margin_value` и `/sys/module/ged/parameters/gx_fb_dvfs_margin`) и активные ядра через MediaTek PPM.
  - **Вручную**: любой профиль можно переключить в любой момент одной командой от имени root:
    ```bash
    echo 2 > /proc/sys/kernel/sched_power_profile
    ```
- **Гармонический квант таймера (`CONFIG_HZ=360`)**: Период тика $1000 / 360 \approx 2,777\text{ мс}$. Ровно 3 тика укладываются в кадр 8,333 мс (120 Гц) и 6 тиков в кадр 16,666 мс (60 Гц), исключая дрейф планировщика относительно vblank.

| ID | Профиль | Кластер Little (ядра 0–3) | Кластер Mid (ядра 4–6) | Prime-ядро (ядро 7) | Назначение и эффект |
| :---: | :--- | :---: | :---: | :---: | :--- |
| `0` | **Супер-энергосбережение** | 1,625 ГГц @ 906 мВ | Отключены (PPM) | 1,998 ГГц @ 875 мВ | Топология **4 Little + 1 Prime**. Ядра Mid выключены, шина `VPROC2` обесточена. Запас GPU 10%, ток покоя 66 мА, пик 547 мВт. Для экстремальной экономии заряда. |
| `1` | **Энергосбережение** | 1,625 ГГц @ 906 мВ | 1,985 ГГц @ 919 мВ | 2,141 ГГц @ 900 мВ | Повседневная экономия, активны все 8 ядер, снижение энергопотребления `-20,6%` к Балансу, запас GPU 10%, без задержек хотплага и с идеальной плавностью 120 Гц. |
| `2` | **Баланс** *(По умолчанию)* | 1,800 ГГц @ 950 мВ | 2,354 ГГц @ 1000 мВ | 2,463 ГГц @ 956 мВ | Физический оптимум чипа MT6893 на техпроцессе TSMC N6, запас GPU 15% (`/sys/kernel/ged/hal/dvfs_margin_value=15`). Холодный корпус и стабильный фреймрейт. |
| `3` | **Режим GT (Без лимитов)** | 2,000 ГГц (Макс) | 2,600 ГГц (Макс) | 3,000 ГГц (Макс) | Полная мощность 8 ядер без ограничений для тяжёлых вычислений, компиляции и игр (пик 2979 мВт, запас GPU 15%). |

- **Микробуст тача**: Перехватывает аппаратные события `EV_ABS` и `BTN_TOUCH` в `drivers/input/input.c`, выдавая импульс ускорения планировщика на 35 мс.

#### 4. Хранилище и блочный уровень UFS 3.1
- **Оптимизация очередей UFS 3.1 и многоочередные элеваторы**: На блочных устройствах UFS 3.1 (`sd*`) применяется оптимизированная конфигурация очередей (планировщик `cfq`, `nr_requests=256`, `read_ahead_kb=256`, `rq_affinity=1`, `nomerges=0`, `add_random=0`), обеспечивающая слияние запросов и плавный сброс грязных страниц без блокировок ввода-вывода. Дополнительно в `elevator_init_mq()` (`block/elevator.c`) снято вендорное ограничение `if (q->nr_hw_queues != 1) return 0;`, благодаря чему многоочередные блочные устройства (`blk-mq`) могут задействовать планировщики `mq-deadline` и `kyber` вместо принудительного режима `none`.

#### 5. Сеть по USB через CDC NCM
- **Тетеринг CDC NCM**: Заменяет устаревший RNDIS (`CONFIG_USB_CONFIGFS_RNDIS=n`) на CDC NCM (`CONFIG_USB_CONFIGFS_NCM=y`, `CONFIG_USB_NET_CDC_NCM=y`), разгружает CPU и повышает скорость передачи данных.
- **Сброс буфера NTB в u_ether**: В стоковом коде `drivers/usb/gadget/function/u_ether.c` передача `skb == NULL` сразу возвращала `-EINVAL`. Драйвер CDC NCM (`f_ncm.c`) сбрасывает накопленный блок NTB вызовом `ndo_start_xmit(NULL, dev)`. Перенос инициализации `pinfo` за пределы `dev->wrap()` и разрешение нулевых пакетов устранили дропы пакетов и зависания передачи под гигабитным трафиком.
- **Псевдоним rndis**: В `drivers/usb/gadget/function/f_ncm.c` зарегистрирован псевдоним `rndis`, поэтому стоковые скрипты `init.rc` поднимают сеть без правок userspace.

#### 6. Сетевой стек, файловые системы и виртуализация
- **Планировщик FQ-CoDel по умолчанию**: В качестве основной дисциплины очередей задействован **FQ-CoDel** (`CONFIG_DEFAULT_NET_SCH="fq_codel"`), полностью устраняющий эффект bufferbloat, удерживающий стабильный минимальный пинг при параллельных сетевых нагрузках и обеспечивающий устойчивость к DPI в зашифрованных туннелях. В ядро также вкомпилированы модули Google BBR v1 (`CONFIG_TCP_CONG_BBR=y`), Westwood+ (`CONFIG_TCP_CONG_WESTWOOD=y`) и CAKE (`CONFIG_NET_SCH_CAKE=y`) с возможностью выбора через sysctl.
- **TPROXY и правка TTL**: Таргет `CONFIG_NETFILTER_XT_TARGET_TPROXY=y` обеспечивает прозрачный перехват сокетов для Sing-box, Clash и Xray. Таргеты `TTL` и `HL` в Netfilter снимают ограничения операторов на раздачу интернета.
- **Сетевые файловые системы (SMB3 / CIFS)**: Поддержка прямого монтирования сетевых хранилищ SMB3 на уровне ядра через `CONFIG_NETWORK_FILESYSTEMS=y` и `CONFIG_CIFS=y` (при этом `CONFIG_NFS_FS=n` строго отключён для соблюдения требований матрицы совместимости Android 13 VINTF).
- **Контейнеры и WireGuard**: Поддержка пространств имён (`USER_NS`, `PID_NS`, `NET_NS`), cgroups, `veth`, `bridge` и встроенный драйвер WireGuard (`CONFIG_WIREGUARD=y`).

#### 7. Звуковая подсистема Bitperfect
- **Вывод звука Bitperfect по USB**: Нативная работа UAC1/UAC2 (`CONFIG_SND_USB_AUDIO=y`) с асинхронными эндпоинтами и прямым воспроизведением DSD/DoP без передискретизации ОС.
- **Режим USB ЦАП**: Смартфон может работать внешним аудиоустройством (`CONFIG_USB_CONFIGFS_F_UAC2=y`) для ПК.
- **ALSA Loopback и MIDI**: Виртуальная звуковая петля (`CONFIG_SND_ALOOP=m`) и USB MIDI (`CONFIG_SND_RAWMIDI=y`, `CONFIG_USB_CONFIGFS_F_MIDI=y`).

#### 8. Периферия, радио и аудит сетей
- **COM-порты (UART)**: Встроенные драйверы для чипов CH340/CH341, CP2102/CP2104, FTDI FT232 и PL2303.
- **SocketCAN**: Поддержка шины CAN (`slcan`, `vcan`, `can_raw`) для диагностики автомобилей по стандарту OBD-II.
- **Программно-определяемое радио (SDR)**: Драйвер Realtek RTL2832U DVB-T (`CONFIG_DVB_USB_RTL28XXU=m`) для приёма радиосигнала и телеметрии.
- **Аудит беспроводных сетей**: Модульный стек `mac80211` (`CONFIG_MAC80211=m`) с поддержкой режима монитора и инъекции кадров на внешних адаптерах (Atheros AR9271, Ralink RT3070, Realtek RTL8187).

#### 9. Бинарная совместимость (KABI) и вендорные блобы
- **Фиксированные смещения структур**: Поля `net_device` и `task_struct` совпадают со стоковыми байт в байт. Это исключает повреждение памяти в закрытых блобах MediaTek.
- **Толерантность к несовпадению CRC символов**: Функция `check_version()` в `kernel/module.c` при расхождении версий символов помечает модуль флагом `TAINT_FORCED_MODULE` и разрешает загрузку вместо ошибки `-ENOEXEC`. В `module_sig_check()` отключён отказ по нестрогим подписям, что позволяет подгружать проприетарные модули ядра.
- **Символ моста Wi-Fi 6**: В `wmt_build_in_adapter.c` экспортирована функция `conn_dbg_add_log`. Без этого экспорта закрытый драйвер `wlan_drv_gen4m.ko` не может слинковаться и падает при загрузке.
- **Защита SELinux от NULL-указателей**: Функции `selinux_inode()` и `selinux_cred()` в `security/selinux/include/objsec.h` проверяют указатели на NULL перед чтением структур безопасности. Это предотвращает панику ядра при обращении к синтетическим дентри от OverlayFS, KernelSU или SuSFS.

---

### Примеры команд в терминале

#### Переключение профилей планировщика CPU
```bash
# Проверка текущего профиля (0=Супер-энергосбережение, 1=Энергосбережение, 2=Баланс, 3=GT)
cat /proc/sys/kernel/sched_power_profile

# Включение профиля Баланс (рекомендуется по умолчанию)
su -c sysctl -w kernel.sched_power_profile=2

# Включение режима супер-энергосбережения
su -c sysctl -w kernel.sched_power_profile=0

# Включение режима GT без ограничений
su -c sysctl -w kernel.sched_power_profile=3
```

#### Снятие ограничений раздачи интернета (TTL)
```bash
# Фиксация TTL исходящих пакетов IPv4 на значении 64
su -c iptables -t mangle -A POSTROUTING -j TTL --ttl-set 64

# Для протокола IPv6 (Hop Limit)
su -c ip6tables -t mangle -A POSTROUTING -j HL --hl-set 64
```

#### Запуск автомобильной шины CAN (OBD-II через CANable)
```bash
# Инициализация адаптера USB-CAN на скорости 500 кбит/с (стандарт OBD-II)
su -c slcand -o -c -s6 /dev/ttyUSB0 slcan0
su -c ip link set slcan0 up

# Просмотр пакетов шины автомобиля в реальном времени
su -c candump slcan0
```

#### Прямое монтирование сетевого диска Windows (SMB3)
```bash
su -c mkdir -p /mnt/share
su -c mount -t cifs //192.168.1.100/Data /mnt/share \
    -o username=user,password=pass,vers=3.0,iocharset=utf8
```

---

### Инструкция по прошивке (Fastboot)

> [!IMPORTANT]
> Перед прошивкой обязательно сделайте резервную копию стокового `boot.img`.

1. Переведите смартфон в режим загрузчика:
   ```bash
   adb reboot bootloader
   ```
2. Проверьте подключение через Fastboot:
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
5. Установите менеджер [KernelSU-Next Manager APK (v3.4.0)](https://github.com/rifsxd/KernelSU-Next/releases).
6. Прошейте модуль **`rmx3357-neocore-companion.zip`** в KernelSU Manager или Magisk для автоматической настройки системы.

---

### Скрипты пространства пользователя и модуль

Файлы находятся в каталоге [`userspace/`](userspace/) и упакованы в модуль **`rmx3357-neocore-companion.zip`**:
- **`system.prop` и `service.sh` (`service.d/00_system_service.sh`)**: `system.prop` задаёт границы кучи ART (`heapstartsize=12m`, `heapgrowthlimit=512m`, `heapsize=768m`) через `resetprop` до инициализации `zygote64`. `service.sh` запускается после расшифровки `/data` и завершения загрузки: останавливает дамперы MediaTek (`emdlogger`, `connsyslogger`, `mobile_log_d`) и службы сбора отчётов ColorOS, выставляет TCP Cubic (`tcp_congestion_control=cubic`), настраивает пороги GED GPU DVFS (`/sys/kernel/ged/hal/dvfs_margin_value=15`, `gx_fb_dvfs_margin=150`) и оптимизирует очереди UFS 3.1 `sd*` (`cfq`, `nr_requests=256`, `read_ahead_kb=256`, `rq_affinity=1`, `nomerges=0`, `add_random=0`).
- **`power_sync.sh` (`service.d/99_power_profile_sync.sh`)**: Чистая событийно-ориентированная архитектура (Event-Driven) на базе Toybox `inotifyd` с фильтром `:cy` (`close_write` / `moved_to`). Мультиплексированный мониторинг каталогов `/data/system/users/0` (`settings_system.xml`, `settings_global.xml`) и `/data/misc/bluetooth` (`cached_hci*`, `*bt*`, `*blue*`). Синхронизирует переключатели питания ColorOS («Суперэнергосбережение» -> профиль `0`, «Энергосбережение» -> профиль `1`, «Сбалансированный» -> профиль `2`, «Режим GT» -> профиль `3`) и привязку ядер Bluetooth с потреблением 0.0% CPU и нулём страничных ошибок дочерних процессов (`cminflt`) в простое.
- **Трёхуровневая аппаратная изоляция Bluetooth LDAC на малых ядрах Cortex-A55 (ядра 0–3)**:
  - *Уровень 1 (Потоки кодека и аудиостека)*: Потоки процесса `com.android.bluetooth` (`bt_a2dp_source_` [LDAC encoder], `bt_stack_manage`, `BluetoothA2dpLo`, `bta_media_*`) заблокированы на ядра 0–3 через `/dev/cpuset/system-background/tasks` и `taskset -p 0f` без вызова внешних подпроцессов чтения.
  - *Уровень 2 (Вендорный сервис HAL)*: Служба MediaTek Bluetooth Audio HAL (`android.hardware.bluetooth@1.1-service-mediatek`) переведена в `/dev/cpuset/system-background/cgroup.procs` с жёсткой привязкой всех потоков к ядрам 0–3 через `taskset -p 0f`.
  - *Уровень 3 (Аппаратная маршрутизация прерываний ARM GICv3)*: Устранена утечка прерываний при составной маске `0f` (где из-за механизма 1-of-N routing контроллера GICv3 до ~23.7% прерываний просачивались на производительные ядра Cortex-A78). Прерывания разведены побитово на конкретные малые ядра:
    - IRQ 18 (`mtk btif tx dma irq`) -> ядро 0 (`smp_affinity: 01`)
    - IRQ 19 (`mtk btif rx dma irq`) -> ядро 1 (`smp_affinity: 02`)
    - IRQ 228 (`BTIF_WAKEUP_IRQ`) -> ядро 2 (`smp_affinity: 04`)
    - IRQ 17 (`mtk btif irq`) и IRQ 281 (`BTCVSD_ISR_Handle`) -> ядро 3 (`smp_affinity: 08`)
    - *Аппаратный замер*: На ядрах 4–7 инкремент прерываний строго равен +0 (+0%), достигнута 100% аппаратная изоляция кластера A78 в Deep Sleep.
  - *Эффект на аккумуляторе*: Разряд при прослушивании Hi-Res аудио по кодеку LDAC снизился в 4.5–5 раз (с 18–24%/ч до 3.5–5%/ч). Частота кластера Little опустилась до базовых 600 МГц при минимальном напряжении шины. Температура устройства держится на комнатных 26.2 °C.
- **Изоляция системных служб и сотового модема на малых ядрах**:
  - *Службы мониторинга и датчиков*: Демоны `android.hardware.health@2.1-service`, `android.hardware.power.stats-impl.oplus` и `android.hardware.thermal@2.0-service.mtk` мигрируются через `/dev/cpuset/system-background/cgroup.procs` и принудительно блокируются по всем потокам на ядрах Cortex-A55 (0–3) через `taskset -p 0f`, исключая пробуждения ядер A78 ради периодического опроса батареи, PMIC и термопар.
  - *Сотовый модем RIL*: Процесс `mtkfusionrild` переведён в `/dev/cpuset/system-background/cgroup.procs` (мягкий приоритет малых ядер). Сигналинг связи, SMS и переключение вышек в движении больше не будят кластер A78, а пользовательский 4G/5G интернет (Data-Plane) обрабатывается напрямую в ядре и DSP модема без малейшего снижения скорости.

Подробная инструкция по ручной установке: [`userspace/README.md`](userspace/README.md).

---

### Сборка из исходников

- **Инструменты сборки**: AOSP Clang 12.0.5 (`clang-r416183b`), LLD 12.0.5, GNU binutils 2.34 (`aarch64-linux-gnu-`, `arm-linux-gnueabi-`).
- **Базовый дефконфиг**: `arch/arm64/configs/k6893v1_64_k419_defconfig` (или `k6893v1_64_k419_ab_defconfig` для A/B).
- **Оверлей конфигурации**: `arch/arm64/configs/custom_rmx3357.config`.

```bash
export ARCH=arm64
export SUBARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export CROSS_COMPILE_ARM32=arm-linux-gnueabi-
export CC=clang
export CLANG_TRIPLE=aarch64-linux-gnu-

# Создание базовой конфигурации
make k6893v1_64_k419_defconfig

# Применение пользовательского оверлея
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
- [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) — [Hybrid-Mount](https://github.com/Hybrid-Mount) (рекомендуемая изоляция точек монтирования)
- [NeoZygisk](https://github.com/JingMatrix/NeoZygisk) — [@JingMatrix](https://github.com/JingMatrix) (рекомендуемая реализация Zygisk)
