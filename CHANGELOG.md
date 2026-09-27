# Changelog / История изменений

[English](#english) | [Русский](#русский)

---

<a name="english"></a>
## English

### Changes between 2026-09-22 and 2026-09-26 (v2026.09.26)

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

### Изменения между сборками от 22.09.2026 и 26.09.2026 (v2026.09.26)

#### 1. Удаление ZeroMount из ядра и возврат стандартной VFS
* **Что сделано**: из исходного кода ядра удалены драйвер ZeroMount и сопутствующие хуки перехвата путей в VFS (`fs/zeromount.c`, `fs/namei.c`, `fs/open.c`).
* **Причина**: перехват путей в VFS нарушал стандартную семантику блокировок POSIX и приводил к повреждению разделяемой памяти WAL-журналирования SQLite (`-wal` / `-shm`). Это вызывало дедлоки и падения с ошибкой `Failed to open database` в приложениях с конкурентными транзакциями SQLite (личные сообщения и кэш TikTok, Telegram, сервисы Google Play).
* **Замена**: восстановлен стандартный слой Linux VFS. Системлесс-монтирование переведено в пространство пользователя через [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) и [NeoZygisk](https://github.com/JingMatrix/NeoZygisk).

#### 2. Доработка частот и профилей планировщика (schedutil)
* **Что сделано**: переработан планировщик частот в `kernel/sched/cpufreq_schedutil.c` и `kernel/sched/cpufreq_schedutil_plus.c` — внедрены аппаратные lockless-лимиты частот кластеров и 4-уровневый переключатель профилей через sysctl (`kernel.sched_power_profile`).
* **Техническое исправление**: вызов `sugov_prime_eeff_cap()` напрямую встроен в `get_next_freq()` внутри `cpufreq_schedutil_plus.c` (активен при `CONFIG_NONLINEAR_FREQ_CTL=y`), а поиск частоты переведён на `CPUFREQ_RELATION_H`, исключая округление частот вверх модулем MediaTek SSPM.
... (+30 lines) [see remaining: rtk proxy git '-C' '/home/gimgiwer/.gemini/antigravity/scratch/kernel-rmx3357/src' show '3df0c569e:CHANGELOG.md' | tail -n +65]
