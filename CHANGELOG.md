# Changelog / История изменений

[English](#english) | [Русский](#русский)

---

<a name="english"></a>
## English

### [v2026.10.30] - 2026-10-30

#### In-Tree KernelSU-Next v3.4.2 (dev b427a3f9, UAPI 5, build 33605)
- Sync in-tree driver with upstream dev (`b427a3f9`): declare UAPI v5 protocol for complete compatibility with Manager v3.4.2.
- Dynamic manager detection: unpin package name across build scripts and runtime tracker to allow arbitrary validly-signed managers.
- Add `KSU_IOCTL_LOOKUP_SYMBOLS` ioctl (#22) and batch in-kernel `kallsyms` lookup (`ksu_lookup_kernel_symbols`) with binary search.
- Retain `#define` macros for UAPI ioctl codes and constants for C userspace switch-case compatibility.
- Harden `is_init_rc()`: filter on `init.rc` dentry name before `d_path()` resolution and add NULL check on `dentry`.
- Prevent `fsnotify` mark release UAF in `pkg_observer` with `.free_mark = ksu_free_mark` and `fsnotify_wait_marks_destroyed()`.
- Route `su_mount_ns` namespace switches (`setns`, `unshare`) through unified `ksyscall` helper.
- Pull in module `preinit` runner with `KSU_NO_PREINIT` stubs for non-GKI trees without installed UAPI headers.
- Return `false` on `kern_path()` error in `throne_tracker` so retry counter does not burn out before `/data` mounts.
- Keep ARM64 16-byte stack alignment with 256-byte gap in `userspace_stack_buffer()`.
- Packaging: inject `androidboot.selinux=permissive enforcing=0` into boot cmdline during repack to prevent Phoenix Watchdog panic.

### [v2026.10.13] - 2026-10-13

#### 1. In-Tree KernelSU-Next v3.4.1+ (dev 0a77550c, UAPI 5, build 33333)
- Update KernelSU-Next to v3.4.1+ dev snapshot (`0a77550c`) with UAPI v5 protocol.
- Integrate scoped descriptors with `EVENT_SERVICES` support.
- Deferred throne tracking via `task_work` and cached APK path lookups.
- In-kernel module blacklisting (`kernelsu.block_modules`) and ADB-only su mode.
- Enforce KernelSU feature locks (`kernel_umount:1`) via boot cmdline injection during packaging.

#### 2. BakaSU SELinux Hiding & Dynamic LSM Hooks
- Backport `selinux_hide` for in-kernel concealment of SELinux status and custom rules from root detection algorithms.
- Implement dynamic symbol resolver (`symbol_resolver.c`) and AArch64 memory patcher (`patch_memory.c`, `lsm_hooks.c`) to hook LSM tables directly in memory without exported `security_hook_heads`.
- Add `adb_root` support, manager APK certificate validation (`apk_sign.c`), and throne tracker package observer.
- Protect root daemon and su sessions against Android process group termination by migrating to init PGID (`KSU_IOCTL_SET_INIT_PGRP`).

#### 3. Virtual Memory, ZRAM & UFS 3.1 Performance Tuning
- Set default `vm_swappiness = 100` to migrate cold anonymous memory to ZRAM and keep physical RAM free for camera bursts while preserving hot file caches.
- Bump `watermark_scale_factor = 150` (1.5% RAM buffer) to eliminate direct reclaim stalls during 120 Hz render bursts.
- Switch default I/O scheduler to `noop` (`CONFIG_DEFAULT_NOOP=y`, `CONFIG_DEFAULT_IOSCHED="noop"`) to leverage UFS 3.1 hardware multi-circular queue (MCQ) dispatching and cut CPU overhead.
- Debloat `service.sh` by removing redundant sysctl overrides handled by kernel defaults.
- Enable 64-bit DEX2OAT compilation via `dalvik.vm.dex2oat64.enabled=true` in `system.prop`.

#### 4. Clang 12 Toolchain Build Fixes
- Fix explicit type casts and signedness warnings in scheduler (`eas_plus.c`), hybridswap (`hybridswap_stats.c`), and `kernel/sysctl.c`.

#### 5. Hardware Diagnostics, Ramoops & USB Serial Console
- Enable `CONFIG_SECURITY_SELINUX_BOOTPARAM=y` to permit boot-time SELinux mode override via `selinux=0` or `enforcing=0` in bootloader cmdline.
- Unmask `CONFIG_PSTORE_RAM=y` to activate Ramoops persistent crash logging across warm reboots in reserved RAM (`0x48090000`).
- Enable kernel console output over hardware USB-UART adapters (`CONFIG_USB_SERIAL_CONSOLE=y`).
- Add virtual COM port support via Android ConfigFS (`CONFIG_USB_CONFIGFS_SERIAL=y`, `CONFIG_U_SERIAL_CONSOLE=y`) and build loadable serial gadget module `g_serial.ko` (`CONFIG_USB_G_SERIAL=m`) while preserving wired ADB and MTP file transfer.
- Enable ring buffer timestamps (`CONFIG_PRINTK_TIME=y`) and runtime driver debug control via `/sys/kernel/debug/dynamic_debug/control` (`CONFIG_DYNAMIC_DEBUG=y`).

#### 6. Standalone Kernel Binary (Image.gz)
- Build and package raw compressed kernel binary `Image.gz` alongside fastboot boot images.
- Designed for custom recovery zip installers (AnyKernel3) and manual repacking with alternative ramdisk/DTB trees without unpacking `boot.img`.

---

### [v2026.10.04] - 2026-10-04

#### 1. Battery Capacity & Health Telemetry
- Report true 4500 mAh dual-cell design capacity (`charge_full_design`) instead of 0 or current capacity.
- Enable OPlus Smart Charge State of Health (`batt_soh` / `soh_support`) tracking nodes.

#### 2. Brownout & Low Battery Throttling
- Cap CPU OPP frequencies via PMIC MT6359/MT6359P DLPT (15% threshold, bat_percent_limit=15) to prevent sudden voltage drop below 3.4V.
- Fix PPM v3 power budget bug that choked CPU power to 600 mW; clamp Little to 1.625 GHz, Mid to 1.985 GHz, and Prime to 2.141 GHz with all 8 cores active.
- Coordinate `cpufreq_schedutil` with `ppm_low_bat_throttle_active` to suppress aggressive frequency ramp-ups under low battery.

#### 3. Touchscreen 360Hz Sampling Persistence
- Cache and restore 360Hz sampling rate across screen blank/unblank cycles directly in the kernel driver.

#### 4. Scheduler s2idle Suspend Stability
- Use `ktime_get_mono_fast_ns()` in `walt_suspend()` to eliminate timekeeping `WARN_ON` warnings and lockups.

#### 5. Wi-Fi Debug Logging Silence
- Mute verbose MediaTek gen4m Wi-Fi debug masks in `wlan_drv_gen4m.ko` and `service.sh`.

---

### [v2026.09.27] - 2026-09-27

#### 1. Scheduler CPU Hotplug Concurrency & RCU Barrier Fix
* **Change**: Resolved a critical kernel panic (`sugov_update_shared+0x160` data abort) triggered by MediaTek PPM dynamic core offlining during deep sleep.
* **Technical Fix**: 
  - In `sugov_stop()`, relocated `WRITE_ONCE(sg_cpu->sg_policy, NULL)` strictly after `synchronize_sched()`, preserving the RCU-sched read-side barrier and preventing active `sugov_update_shared()` workers from reading a torn or nullified policy pointer.
  - Implemented early defensive NULL guards in `sugov_update_shared()` and `sugov_update_single()` before acquiring `sg_policy->update_lock`.
  - Refactored `sugov_next_freq_shared()` to accept a pre-validated `sg_policy` pointer and guarded `policy` and `policy->cpus` traversal against hotplug races.
  - Eliminated a latent `might_sleep()` lock ordering issue in `apply_sched_power_profile()` by moving `sugov_notify_eeff_cap_changed()` into the sleepable path after unlocking `sched_profile_mutex`.

#### 2. Wave 8 Comprehensive Stability & Security Audit
* **Audit Verdict**: 100% CLEAN across all core and hardware subsystems (0 panics, 0 race conditions, 0 memory leaks).
* **Audited Subsystems**: USB Gadget NCM disconnect concurrency, Netfilter TPROXY socket refcounting invariants, WireGuard crypto memory zeroing, Input touch boost rate limiting, SuSFS 1.5.5 RCU dcache lookups, and KernelSU-Next v3.4.0 descriptor scoping.

#### 3. Standalone Chinese Market Name Neutralization
* **Change**: Added autonomous pre-Zygote script `/data/adb/post-fs-data.d/01_clean_device_name.sh`.
* **Technical Fix**: Directly applies `resetprop -n ro.vendor.oplus.market.name "realme GT Neo 2T"` and `ro.product.marketname` before UI initialization, eliminating raw Chinese characters (`真我GT Neo2T`) from ColorOS Settings while keeping Play Integrity certification intact.

---

### [v2026.09.26] - 2026-09-26

#### 1. Remove ZeroMount and Restore VFS
* **Change**: Removed experimental in-tree ZeroMount driver and associated VFS path-redirection hooks (`fs/zeromount.c`, `fs/namei.c`, `fs/open.c`).
* **Root Cause & Rationale**: ZeroMount inline VFS path interception violated POSIX file locking semantics and corrupted SQLite WAL (Write-Ahead Logging) shared-memory mapping (`-wal` / `-shm`). This triggered severe database lockups and fatal `Failed to open database` crashes in applications with concurrent SQLite transactions (TikTok direct messages and media cache, Telegram, and Google Play Services).
* **Replacement**: Restored standard Linux VFS. Systemless root and module overlays moved to userspace via [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) and [NeoZygisk](https://github.com/JingMatrix/NeoZygisk).

#### 2. CPU Frequency Scaling and Schedutil Profile Updates
* **Change**: Updated cpufreq governor logic in `kernel/sched/cpufreq_schedutil.c` and `kernel/sched/cpufreq_schedutil_plus.c` with lockless per-cluster ceilings and a 4-level runtime sysctl (`kernel.sched_power_profile`).
* **Technical Fix**: Integrated `sugov_prime_eeff_cap()` into `get_next_freq()` within `cpufreq_schedutil_plus.c` under `CONFIG_NONLINEAR_FREQ_CTL=y`, clamping frequencies via `CPUFREQ_RELATION_H` to avoid upward rounding in MediaTek SSPM.
* **Tuning**: Calibrated against MT6893 TSMC N6 hardware OPP tables — Super Power Saving (`0`: Little 1.625 GHz @ 906 mV, Mid offline via PPM, Prime 1.998 GHz @ 875 mV), Power Saving (`1`: Little 1.625 GHz @ 906 mV, Mid 1.985 GHz @ 919 mV, Prime 2.141 GHz @ 900 mV, `-20.6%` energy reduction vs Balanced), Balanced (`2`: Little 1.800 GHz @ 950 mV, Mid 2.354 GHz @ 1000 mV, Prime 2.463 GHz @ 956 mV), and GT Mode (`3`: uncapped `2000 / 2600 / 3000 MHz`). Set transition delays to 20–30 ms down-rate and 500 µs up-rate with sysfs write protection against PowerHAL/uclamp_ctrl resets.

#### 3. Display Timer Configuration (CONFIG_HZ=360)
* **Change**: Configured system timer frequency to 360 Hz (`CONFIG_HZ_360=y`, `CONFIG_HZ=360`).
* **Rationale**: Tick period is $1000 / 360 \approx 2.777\text{ ms}$. Exactly 3 ticks fit into an 8.333 ms frame (120 Hz) and 6 ticks into a 16.666 ms frame (60 Hz), preventing timer phase drift against SurfaceFlinger vblank deadlines and reducing frame rendering jitter.

#### 4. Touch Boost and Debounce Logic
* **Change**: Modified touch boost trigger logic in `drivers/input/input.c`.
* **Technical Fix**: Limited event filtering to `EV_ABS` with `BTN_TOUCH` or `INPUT_PROP_DIRECT`, excluding accelerometer and gyroscope sensor interrupts. Replaced non-atomic timestamp checks with `atomic_long_cmpxchg()` 60 ms debounce. Synchronized with vendor Oplus EAS via `sysctl_input_boost_enabled = 1` and `sched_assist_input_boost_duration = now + 35 ms`.

#### 5. USB CDC NCM Networking and Qdisc Use-After-Free Fix
* **Change**: Replaced legacy RNDIS with USB CDC NCM tethering (`CONFIG_USB_CONFIGFS_NCM=y`, `CONFIG_USB_NET_CDC_NCM=y`) and resolved kernel network gadget panics.
* **Technical Fix**: Unified module registration in `drivers/usb/gadget/function/f_ncm.c` with safe rollback. In `drivers/usb/gadget/function/u_ether.c`, fixed Qdisc `dev_requeue_skb` Use-After-Free by freeing skb and returning `NETDEV_TX_OK` with `tx_dropped++` on queue full instead of `NETDEV_TX_BUSY`. Enforced `hrtimer_cancel` prior to netdev teardown.

#### 6. SuSFS v1.5.5 VFS and RCU Updates
* **Change**: Integrated and stabilized SuSFS 1.5.5 kernel stealth subsystem across 32 core files.
* **Technical Fix**: Added missing `drop_super(s)` on `-EINVAL` exit path in `fs/statfs.c:vfs_ustat()`. Populated all standard fields in `fs/stat.c:generic_fillattr()` before spoofing to eliminate uninitialized kernel stack disclosures. Converted `sus_path`, `sus_kstat`, and `open_redirect` hash tables to RCU (`hash_add_rcu`, `hash_del_rcu`, `kfree_rcu`, `rcu_read_lock`). Extracted `kern_path` and `GFP_KERNEL` allocations from spinlocks, and restricted `prctl` and `reboot` supercalls to `current_uid().val == 0`.

#### 7. Root Architecture: KernelSU-Next v3.4.0-legacy (UAPI 4)
* **Change**: Direct in-tree integration of KernelSU-Next v3.4.0-legacy (`CONFIG_KSU_MANUAL_HOOK=y`) with scoped descriptors (`[ksu_driver_su]`).
* **Technical Fix**: Corrected mount root validation in `ksu_try_umount()`, eliminating `KernelSU: umount /data/adb/modules failed: -22` spam during zygote app spawning. Gated `TASK_STRUCT_NON_ROOT_USER_APP_PROC` to `new_uid >= 10000` to prevent taining system daemons. Connected `ksu_handle_slow_avc_audit()` into SELinux AVC.

#### 8. Network, Filesystems & Storage
* **Change**: Restored **Cubic** (`CONFIG_DEFAULT_TCP_CONG="cubic"`) as default TCP congestion control paired with **FQ-CoDel** (`CONFIG_DEFAULT_NET_SCH="fq_codel"`).
* **Technical Fix**: Disabled `CONFIG_NFS_FS` to maintain Android 13 VINTF compatibility. Enabled `CONFIG_NETWORK_FILESYSTEMS=y` to provide kernel-level SMB3/CIFS mounting (`CONFIG_CIFS=y`). Tuned UFS 3.1 block request queues to depth 256.

#### 9. Companion Module & Userspace Automation
* **Change**: Synchronized `rmx3357-neocore-companion` module with kernel Ring 0 controls.
* **Technical Fix**: Added `:cy` event mask (`close_write` / `moved_to`) to Toybox `inotifyd` in `power_sync.sh`, reducing idle CPU consumption from 4–5% down to **0.0%** and stopping settings read-loop amplification. Created `system.prop` for early ART Dalvik VM heap configuration before Zygote launch. Converted Bluetooth LDAC thread migration to fork-free shell built-ins and synchronized dual GED GPU DVFS margin nodes.

---

<a name="русский"></a>
## Русский

### [v2026.10.30] - 30.10.2026

#### Обновление KernelSU-Next до v3.4.2 (dev b427a3f9, UAPI 5, сборка 33605)
- Синхронизация с веткой dev (`b427a3f9`): возврат на объявление протокола UAPI v5 для полной совместимости с приложением KernelSU-Next Manager v3.4.2.
- Динамическое определение менеджера: удаление жёсткой привязки имени пакета (`KSU_MANAGER_PACKAGE`) в сборочных файлах и ядре для поддержки любых валидно подписанных менеджеров.
- Добавлен суперколл `KSU_IOCTL_LOOKUP_SYMBOLS` (#22) и пакетный резолвер символов ядра (`ksu_lookup_kernel_symbols`) через бинарный поиск по `kallsyms`.
- Сохранены стандартные макросы `#define` для кодов ioctl и констант UAPI ради совместимости с конструкциями `switch/case` в программах юзерспейса на C.
- Усилена проверка `is_init_rc()`: быстрый фильтр по имени `init.rc` до вызова `d_path()` и проверка указателя dentry на NULL.
- Устранён UAF при сбросе меток `fsnotify` в `pkg_observer`: добавлен коллбэк `.free_mark = ksu_free_mark` и барьер `fsnotify_wait_marks_destroyed()`.
- Переключение пространств имён в `su_mount_ns` (`setns`, `unshare`) переведено на общий макрос `ksyscall`.
- Добавлен ранний раннер `preinit` со сборкой под заглушки `KSU_NO_PREINIT`, если в дереве нет готовых заголовков UAPI.
- В `throne_tracker` возвращаем `false` при ошибке `kern_path()`, чтобы счётчик попыток не сгорал до монтирования `/data`.
- Сохранено 16-байтовое выравнивание стека ARM64 с запасом 256 байт в `userspace_stack_buffer()`.
- Сборка и упаковка: автоматическое внедрение `androidboot.selinux=permissive enforcing=0` в cmdline при перепаковке `boot.img` для предотвращения паники Phoenix Watchdog.

### [v2026.10.13] - 13.10.2026

#### 1. Обновление KernelSU-Next до v3.4.1+ (dev 0a77550c, UAPI 5, build 33333)
- Обновление встроенного KernelSU-Next до dev-среза v3.4.1+ (`0a77550c`) с протоколом UAPI v5.
- Поддержка изолированных дескрипторов с событием `EVENT_SERVICES`.
- Отложенная проверка менеджера (throne tracking) через `task_work` и кэширование путей APK.
- Блокировка модулей на уровне ядра (`kernelsu.block_modules`) и ограничение `su` только сессиями ADB.
- Аппаратная фиксация функций KernelSU (`kernel_umount:1`) через cmdline заголовка boot.img при сборке.

#### 2. Скрытие SELinux и динамические LSM-хуки (BakaSU)
- Модуль `selinux_hide` в ядре динамически скрывает статус и добавленные правила SELinux от проверок рут-детекта.
- Динамический резолвер символов (`symbol_resolver.c`) и патчер памяти AArch64 (`patch_memory.c`, `lsm_hooks.c`) внедряют хуки в таблицы LSM напрямую в памяти ядра без необходимости экспорта `security_hook_heads`.
- Поддержка `adb_root`, проверка подлинности APK менеджера по сертификату (`apk_sign.c`) и отслеживание установки пакетов (throne tracker).
- Защита демона и сессий root от принудительного завершения системой Android путём переноса в PGID процесса init (`KSU_IOCTL_SET_INIT_PGRP`).

#### 3. Тюнинг виртуальной памяти, ZRAM и UFS 3.1
- Значение по умолчанию `vm_swappiness = 100` для своевременного вытеснения неактивных страниц в ZRAM и сохранения свободной оперативной памяти под пиковые нагрузки (съёмка фото, запуск приложений).
- Значение по умолчанию `watermark_scale_factor = 150` (буфер 1.5% RAM) для исключения задержек прямого сброса страниц (direct reclaim) при частоте обновления 120 Гц.
- Планировщик по умолчанию `noop` (`CONFIG_DEFAULT_NOOP=y`, `CONFIG_DEFAULT_IOSCHED="noop"`) под аппаратные очереди контроллера UFS 3.1 (MCQ), разгружающий процессор от ненужной программной пересортировки.
- Очистка `service.sh` от избыточных вызовов sysctl, параметры которых встроены в ядро по умолчанию.
- Включение 64-битной компиляции DEX2OAT (`dalvik.vm.dex2oat64.enabled=true`) в `system.prop`.

#### 4. Исправление сборки под Clang 12
- Устранены предупреждения о типах и знаковости в `eas_plus.c`, `hybridswap_stats.c` и `kernel/sysctl.c`.

#### 5. Диагностика, краш-дампы в ОЗУ (Ramoops) и USB-консоль
- Включён `CONFIG_SECURITY_SELINUX_BOOTPARAM=y` для переопределения режима SELinux на этапе загрузки аргументом `selinux=0` в cmdline.
- Разблокирован `CONFIG_PSTORE_RAM=y` для сохранения дампов паник и сообщений `dmesg` (Ramoops) в зарезервированной области ОЗУ (`0x48090000`) при мягкой перезагрузке.
- Включён вывод ранней консоли ядра на аппаратные адаптеры USB-UART (`CONFIG_USB_SERIAL_CONSOLE=y`).
- Добавлена поддержка виртуального COM-порта в Android ConfigFS (`CONFIG_USB_CONFIGFS_SERIAL=y`, `CONFIG_U_SERIAL_CONSOLE=y`) и сборка модуля `g_serial.ko` (`CONFIG_USB_G_SERIAL=m`) без нарушения работы проводного ADB и передачи MTP.
- Включены временные метки сообщений (`CONFIG_PRINTK_TIME=y`) и динамическое управление отладкой драйверов на лету через `/sys/kernel/debug/dynamic_debug/control` (`CONFIG_DYNAMIC_DEBUG=y`).

#### 6. Поставка чистого бинарника ядра Image.gz
- В пайплайн CI добавлена публикация чистого сжатого ядра Linux `Image.gz` отдельно от готового образа `boot.img`.
- Предназначен для использования в кастомных установщиках (AnyKernel3) и ручной перепаковки с альтернативными ramdisk или DTB без необходимости предварительной распаковки готового `boot.img`.
- Для стандартной прошивки через fastboot по-прежнему используется готовый перепакованный образ раздела `boot-rmx3357-neocore.img`.

---

### [v2026.10.04] - 04.10.2026

#### 1. Ёмкость аккумулятора и телеметрия износа (SOH)
- Отображение честной паспортной ёмкости 4500 мА·ч (`charge_full_design`) вместо 0 или текущей ёмкости.
- Включение узлов OPlus Smart Charge State of Health (`batt_soh` / `soh_support`) для замера износа батареи.

#### 2. Защита от внезапного отключения (DLPT) и троттлинг
- Ограничение верхних частот OPP через DLPT на PMIC MT6359/MT6359P (порог 15%, параметр bat_percent_limit=15) для защиты от просадки напряжения ниже 3.4 В.
- Исправление бага MediaTek PPM v3: вместо удушения бюджета до 600 мВт все 8 ядер остаются активными с мягкими потолками (Little 1.625 ГГц, Mid 1.985 ГГц, Prime 2.141 ГГц).
- Координация `cpufreq_schedutil` с флагом `ppm_low_bat_throttle_active` для подавления резких скачков частот при низком заряде.

#### 3. Сохранение частоты опроса тача 360 Гц
- Кэширование и автоматический возврат режима 360 Гц при включении экрана прямо в драйвере ядра.

#### 4. Стабильность планировщика в Suspend-to-Idle (s2idle)
- Перевод замера времени в `walt_suspend()` на `ktime_get_mono_fast_ns()` для устранения предупреждений `WARN_ON`.

#### 5. Отключение отладочного спама Wi-Fi
- Глушение избыточных дебаг-масок в драйвере `wlan_drv_gen4m.ko` и скрипте `service.sh`.

---

### [v2026.09.27] - 27.09.2026

#### 1. Устранение гонки CPU Hotplug в планировщике и RCU-барьер
* **Что сделано**: ликвидирована критическая паника ядра (`sugov_update_shared+0x160` data abort), возникавшая при отключении средних ядер процессора подсистемой MediaTek PPM при уходе смартфона в глубокий сон.
* **Техническое исправление**:
  - В функции `sugov_stop()` зануление указателя `WRITE_ONCE(sg_cpu->sg_policy, NULL)` перенесено **строго за пределы барьера `synchronize_sched()`**. Это исключает ситуацию, когда параллельно работающий поток планировщика считывает `NULL` прямо во время вычисления частоты.
  - Добавлены предварительные защитные проверки `READ_ONCE(sg_cpu->sg_policy)` и `if (unlikely(!policy))` в `sugov_update_shared()` и `sugov_update_single()` до захвата спинлока `update_lock`.
  - В `sugov_next_freq_shared()` проверенный указатель `sg_policy` теперь передаётся аргументом, предотвращая повторное вычитывание из памяти, и защищён обход `policy->cpus`.
  - Устранена потенциальная блокировка `might_sleep()` в `apply_sched_power_profile()`: вызов `sugov_notify_eeff_cap_changed()` изолирован внутри ветки со спящим контекстом и перенесён за `mutex_unlock(&sched_profile_mutex)`.

#### 2. Детальный аудит безопасности и стабильности (Wave 8)
* **Вердикт аудита**: 100% CLEAN по всем подсистемам (0 паник ядра, 0 утечек памяти, 0 дедлоков).
* **Проверенные подсистемы**: конкурентность отключения USB Gadget NCM на полной скорости передачи данных, рефкаунты сокетов в Netfilter TPROXY, криптографическая очистка памяти в WireGuard, дебаунс тача в Input, RCU-обход дентри в SuSFS 1.5.5 и аутентификация суперколла в KernelSU-Next.

#### 3. Автономная нейтрализация китайских иероглифов в имени модели
* **Что сделано**: добавлен автономный скрипт `/data/adb/post-fs-data.d/01_clean_device_name.sh`.
* **Техническое исправление**: на этапе `post-fs-data` до запуска Zygote и графической оболочки динамически выставляет `ro.vendor.oplus.market.name = realme GT Neo 2T` и `ro.product.marketname`, убирая иероглифы `真我GT Neo2T` из настроек ColorOS без модификации системных разделов и с сохранением сертификации Play Integrity.

---

### [v2026.09.26] - 26.09.2026

#### 1. Удаление ZeroMount из ядра и возврат стандартной VFS
* **Что сделано**: из исходного кода ядра удалены драйвер ZeroMount и сопутствующие хуки перехвата путей в VFS (`fs/zeromount.c`, `fs/namei.c`, `fs/open.c`).
* **Причина**: перехват путей в VFS нарушал стандартную семантику блокировок POSIX и приводил к повреждению разделяемой памяти WAL-журналирования SQLite (`-wal` / `-shm`). Это вызывало дедлоки и падения с ошибкой `Failed to open database` в приложениях с конкурентными транзакциями SQLite (личные сообщения и кэш TikTok, Telegram, сервисы Google Play).
* **Замена**: восстановлен стандартный слой Linux VFS. Системлесс-монтирование переведено в пространство пользователя через [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) и [NeoZygisk](https://github.com/JingMatrix/NeoZygisk).

#### 2. Доработка частот и профилей планировщика (schedutil)
* **Что сделано**: переработан планировщик частот в `kernel/sched/cpufreq_schedutil.c` и `kernel/sched/cpufreq_schedutil_plus.c` — внедрены аппаратные lockless-лимиты частот кластеров и 4-уровневый переключатель профилей через sysctl (`kernel.sched_power_profile`).
* **Техническое исправление**: вызов `sugov_prime_eeff_cap()` напрямую встроен в `get_next_freq()` внутри `cpufreq_schedutil_plus.c` (активен при `CONFIG_NONLINEAR_FREQ_CTL=y`), а поиск частоты переведён на `CPUFREQ_RELATION_H`, исключая округление частот вверх модулем MediaTek SSPM.
* **Тюнинг**: калибровка выполнена под аппаратные таблицы OPP MT6893 TSMC N6: Супер-энергосбережение (`0`: Little 1.625 ГГц при 906 мВ, Mid выключены через PPM, Prime 1.998 ГГц при 875 мВ), Энергосбережение (`1`: Little 1.625 ГГц при 906 мВ, Mid 1.985 ГГц при 919 мВ, Prime 2.141 ГГц при 900 мВ, на `-20.6%` экономичнее Баланса), Баланс (`2`: Little 1.800 ГГц при 950 мВ, Mid 2.354 ГГц при 1000 мВ, Prime 2.463 ГГц при 956 мВ) и Режим GT (`3`: частоты без ограничений `2000 / 2600 / 3000 МГц`). Задержки сброса частоты выставлены в 20–30 мс, задержка роста — 500 мкс с защитой от сброса через PowerHAL/uclamp_ctrl.

#### 3. Дисплейный таймер HZ=360 для экранов 120 Гц
* **Что сделано**: частота таймера ядра переведена на 360 Гц (`CONFIG_HZ_360=y`, `CONFIG_HZ=360`).
* **Причина**: период тика составляет 2.777 мс. Ровно 3 тика укладываются в кадр 120 Гц (8.333 мс) и 6 тиков в кадр 60 Гц (16.666 мс), устраняя рассинхронизацию с дедлайнами vblank SurfaceFlinger и микростаттеры рендеринга.

#### 4. Дебаунс и логика микробуста тача
* **Что сделано**: модифицирован триггер тач-буста в `drivers/input/input.c`.
* **Техническое исправление**: фильтрация событий ограничена только `EV_ABS` с `BTN_TOUCH` или `INPUT_PROP_DIRECT`, исключая прерывания датчиков движения. Неатомарные проверки заменены на атомарный 60 мс дебаунс через `atomic_long_cmpxchg()`. Добавлена синхронизация с вендорным Oplus EAS через `sysctl_input_boost_enabled = 1` и `sched_assist_input_boost_duration = now + 35 ms`.

#### 5. Сеть USB CDC NCM и исправление Use-After-Free в Qdisc
* **Что сделано**: устаревший протокол RNDIS заменён на USB CDC NCM (`CONFIG_USB_CONFIGFS_NCM=y`, `CONFIG_USB_NET_CDC_NCM=y`) с устранением паник сетевого гаджета.
* **Техническое исправление**: унифицирована регистрация модуля в `drivers/usb/gadget/function/f_ncm.c`. В `drivers/usb/gadget/function/u_ether.c` устранён Use-After-Free при возврате `NETDEV_TX_BUSY`: теперь при переполнении очереди пакет освобождается с возвратом `NETDEV_TX_OK` и инкрементом `tx_dropped++`. Вызов `hrtimer_cancel` строго выполняется до завершения работы сетевого интерфейса.

#### 6. Стабилизация SuSFS v1.5.5 в VFS и RCU
* **Что сделано**: выполнена интеграция подсистемы сокрытия SuSFS 1.5.5 по 32 файлам ядра.
* **Техническое исправление**: добавлен недостающий `drop_super(s)` при выходе по ошибке `-EINVAL` в `fs/statfs.c:vfs_ustat()`. Все стандартные поля в `fs/stat.c:generic_fillattr()` инициализируются до подмены для предотвращения утечек неинициализированной памяти стека ядра. Хэш-таблицы `sus_path`, `sus_kstat` и `open_redirect` переведены на RCU (`hash_add_rcu`, `hash_del_rcu`, `kfree_rcu`, `rcu_read_lock`). Вызовы `kern_path` и аллокации `GFP_KERNEL` вынесены из-под спинлоков, а суперколлы `prctl` и `reboot` ограничены проверкой `current_uid().val == 0`.

#### 7. Архитектура Root: KernelSU-Next v3.4.0-legacy (UAPI 4)
* **Что сделано**: прямая in-tree интеграция драйвера KernelSU-Next v3.4.0-legacy (`CONFIG_KSU_MANUAL_HOOK=y`) с дескрипторами `[ksu_driver_su]`.
* **Техническое исправление**: исправлена валидация точки монтирования в `ksu_try_umount()`, что устранило спам `KernelSU: umount /data/adb/modules failed: -22` при спавне приложений из Zygote. Выставление `TASK_STRUCT_NON_ROOT_USER_APP_PROC` ограничено UID `>= 10000`. Вызов `ksu_handle_slow_avc_audit()` подключён к AVC SELinux.

#### 8. Сетевой стек, файловые системы и накопитель
* **Что сделано**: возвращён **Cubic** (`CONFIG_DEFAULT_TCP_CONG="cubic"`) в связке с **FQ-CoDel** (`CONFIG_DEFAULT_NET_SCH="fq_codel"`).
* **Техническое исправление**: отключён `CONFIG_NFS_FS` для прохождения VINTF Android 13. Включён `CONFIG_NETWORK_FILESYSTEMS=y` для монтирования сетевых дисков SMB3/CIFS (`CONFIG_CIFS=y`). Глубина очередей UFS 3.1 увеличена до 256.

#### 9. Модуль-компаньон и автоматизация
* **Что сделано**: синхронизирован модуль `rmx3357-neocore-companion` с механизмами ядра.
* **Техническое исправление**: добавлен фильтр событий `:cy` (`close_write` / `moved_to`) для `inotifyd` в `power_sync.sh`, что снизило потребление CPU в простое с 4–5% до **0.0%** и устранило лавинообразные циклы чтения настроек. Создан `system.prop` для ранней настройки кучи Dalvik до Zygote. Миграция потоков Bluetooth LDAC переведена на встроенные конструкции шелла без форков, синхронизированы оба узла маржи GED GPU DVFS.
