# Changelog / История изменений

[English](#english) | [Русский](#русский)

---

<a name="english"></a>
## English

### Changes between 2026-09-22 and 2026-09-26 (v2026.09.26)

#### 1. ZeroMount Excised from Kernel & POSIX VFS Cleanliness
* **Change**: Completely removed the experimental in-tree ZeroMount driver and all associated VFS path-redirection hooks (`fs/zeromount.c`, `fs/namei.c`, `fs/open.c`).
* **Root Cause & Rationale**: ZeroMount's inline VFS path interception broke POSIX file locking invariants and corrupted SQLite WAL (Write-Ahead Logging) shared-memory mapping (`-wal` / `-shm`). This triggered severe database lockups and fatal `Failed to open database` crashes in applications with concurrent SQLite transactions (TikTok direct messages and media cache, Telegram, and Google Play Services).
* **Replacement**: Pure unmodified Linux VFS restored. Systemless root and module overlays moved cleanly to userspace via [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) and [NeoZygisk](https://github.com/JingMatrix/NeoZygisk).

#### 2. CPU Frequency Scaling & Schedutil Profiles Refinement
* **Change**: Overhauled cpufreq governor logic in `kernel/sched/cpufreq_schedutil.c` and `kernel/sched/cpufreq_schedutil_plus.c` with lockless per-cluster ceilings and a 4-level runtime sysctl (`kernel.sched_power_profile`).
* **Technical Fix**: Directly wired `sugov_prime_eeff_cap()` into `get_next_freq()` within `cpufreq_schedutil_plus.c` under `CONFIG_NONLINEAR_FREQ_CTL=y`, clamping frequencies via `CPUFREQ_RELATION_H` to prevent upward rounding in MediaTek SSPM.
* **Tuning**: Calibrated against MT6893 TSMC N6 hardware OPP tables — Super Power Saving (`0`: Little 1.625 GHz @ 906 mV, Mid offline via PPM, Prime 1.998 GHz @ 875 mV), Power Saving (`1`: Little 1.625 GHz @ 906 mV, Mid 1.985 GHz @ 919 mV, Prime 2.141 GHz @ 900 mV, `-20.6%` energy reduction vs Balanced), Balanced (`2`: Little 1.800 GHz @ 950 mV, Mid 2.354 GHz @ 1000 mV, Prime 2.463 GHz @ 956 mV), and GT Mode (`3`: uncapped `2000 / 2600 / 3000 MHz`). Transition delays optimized to 20–30 ms down-rate and 500 µs up-rate with sysfs write protection against PowerHAL/uclamp_ctrl resets.

#### 3. Harmonic Display Timer (CONFIG_HZ=360)
* **Change**: Configured system timer frequency to 360 Hz (`CONFIG_HZ_360=y`, `CONFIG_HZ=360`).
* **Rationale**: Tick period is $1000 / 360 \approx 2.777\text{ ms}$. Exactly 3 ticks fit into an 8.333 ms frame (120 Hz) and 6 ticks into a 16.666 ms frame (60 Hz), eliminating timer phase drift against SurfaceFlinger vblank deadlines and ensuring jitter-free frame rendering.

#### 4. Input Touch Microboost & Race-Free Debounce
* **Change**: Hardened touch boost trigger logic in `drivers/input/input.c`.
* **Technical Fix**: Restricted event filtering to `EV_ABS` with `BTN_TOUCH` or `INPUT_PROP_DIRECT`, ignoring accelerometer and gyroscope sensor interrupts. Replaced non-atomic timestamp checks with race-free `atomic_long_cmpxchg()` 60 ms debounce. Synchronized with vendor Oplus EAS via `sysctl_input_boost_enabled = 1` and `sched_assist_input_boost_duration = now + 35 ms`.

#### 5. USB CDC NCM Networking & Qdisc Use-After-Free Elimination
* **Change**: Replaced legacy RNDIS with USB CDC NCM tethering (`CONFIG_USB_CONFIGFS_NCM=y`, `CONFIG_USB_NET_CDC_NCM=y`) and resolved kernel network gadget panics.
* **Technical Fix**: Unified module registration in `drivers/usb/gadget/function/f_ncm.c` with safe rollback. In `drivers/usb/gadget/function/u_ether.c`, fixed Qdisc `dev_requeue_skb` Use-After-Free by freeing skb and returning `NETDEV_TX_OK` with `tx_dropped++` on queue full instead of `NETDEV_TX_BUSY`. Enforced `hrtimer_cancel` prior to netdev teardown.

#### 6. Stealth & VFS Concurrency: SuSFS v1.5.5 Subsystem Hardening
* **Change**: Integrated and stabilized SuSFS 1.5.5 kernel stealth subsystem across 32 core files.
* **Technical Fix**: Added missing `drop_super(s)` on `-EINVAL` exit path in `fs/statfs.c:vfs_ustat()`. Populated all standard fields in `fs/stat.c:generic_fillattr()` before spoofing to eliminate uninitialized kernel stack disclosures. Converted `sus_path`, `sus_kstat`, and `open_redirect` hash tables to RCU (`hash_add_rcu`, `hash_del_rcu`, `kfree_rcu`, `rcu_read_lock`). Extracted `kern_path` and `GFP_KERNEL` allocations from spinlocks, and restricted `prctl` and `reboot` supercalls to `current_uid().val == 0`.

#### 7. Root Architecture: KernelSU-Next v3.4.0-legacy (UAPI 4)
* **Change**: Direct in-tree integration of KernelSU-Next v3.4.0-legacy (`CONFIG_KSU_MANUAL_HOOK=y`) with scoped descriptors (`[ksu_driver_su]`).
* **Technical Fix**: Corrected mount root validation in `ksu_try_umount()`, eliminating `KernelSU: umount /data/adb/modules failed: -22` spam during zygote app spawning. Gated `TASK_STRUCT_NON_ROOT_USER_APP_PROC` to `new_uid >= 10000` to prevent taining system daemons. Connected `ksu_handle_slow_avc_audit()` into SELinux AVC.

#### 8. Network, Filesystems & Google VINTF FCM Level 5 Compliance
* **Change**: Restored **Cubic** (`CONFIG_DEFAULT_TCP_CONG="cubic"`) as default TCP congestion control paired with **FQ-CoDel** (`CONFIG_DEFAULT_NET_SCH="fq_codel"`).
* **Technical Fix**: Strictly disabled `CONFIG_NFS_FS` (`# CONFIG_NFS_FS is not set`), ensuring full compliance with Google Android 13 VINTF FCM Level 5 and eliminating framework compatibility error dialogs. Enabled `CONFIG_NETWORK_FILESYSTEMS=y` to provide kernel-level SMB3/CIFS mounting (`CONFIG_CIFS=y`). Tuned UFS 3.1 block request queues to depth 256.

#### 9. Companion Module & Userspace Automation
* **Change**: Synchronized `rmx3357-neocore-companion` module with kernel Ring 0 controls.
* **Technical Fix**: Added `:cy` event mask (`close_write` / `moved_to`) to Toybox `inotifyd` in `power_sync.sh`, reducing idle CPU consumption from 4–5% down to **0.0%** and stopping settings read-loop amplification. Created `system.prop` for early ART Dalvik VM heap configuration before Zygote launch. Converted Bluetooth LDAC thread migration to fork-free shell built-ins and synchronized dual GED GPU DVFS margin nodes.

---

<a name="русский"></a>
## Русский

### Изменения между сборками от 22.09.2026 и 26.09.2026 (v2026.09.26)

#### 1. Полное удаление ZeroMount из ядра и чистота VFS
* **Что сделано**: из исходного кода ядра полностью вырезаны драйвер ZeroMount и сопутствующие хуки перехвата путей в VFS (`fs/zeromount.c`, `fs/namei.c`, `fs/open.c`).
* **Причина**: перехват путей в VFS нарушал стандартную семантику блокировок POSIX и приводил к повреждению разделяемой памяти WAL-журналирования SQLite (`-wal` / `-shm`). Это вызывало дедлоки и падения с ошибкой `Failed to open database` в приложениях с конкурентными транзакциями SQLite (личные сообщения и кэш TikTok, Telegram, сервисы Google Play).
* **Замена**: восстановлен чистый слой Linux VFS. Системлесс-монтирование переведено в пространство пользователя через [Hybrid Mount](https://github.com/Hybrid-Mount/meta-hybrid_mount) и [NeoZygisk](https://github.com/JingMatrix/NeoZygisk).

#### 2. Доработка частот и профилей планировщика (schedutil)
* **Что сделано**: переработан планировщик частот в `kernel/sched/cpufreq_schedutil.c` и `kernel/sched/cpufreq_schedutil_plus.c` — внедрены аппаратные lockless-лимиты частот кластеров и 4-уровневый переключатель профилей через sysctl (`kernel.sched_power_profile`).
* **Техническое исправление**: вызов `sugov_prime_eeff_cap()` напрямую встроен в `get_next_freq()` внутри `cpufreq_schedutil_plus.c` (активен при `CONFIG_NONLINEAR_FREQ_CTL=y`), а поиск частоты переведён на `CPUFREQ_RELATION_H`, исключая округление частот вверх модулем MediaTek SSPM.
* **Калибровка**: профили согласованы с аппаратными таблицами OPP чипа MT6893 (TSMC N6) — «Суперэнергосбережение» (`0`: Little 1,625 ГГц @ 906 мВ, Mid отключены через PPM, Prime 1,998 ГГц @ 875 мВ), «Энергосбережение» (`1`: Little 1,625 ГГц @ 906 мВ, Mid 1,985 ГГц @ 919 мВ, Prime 2,141 ГГц @ 900 мВ, экономия `-20,6%` к Балансу), «Баланс» (`2`: Little 1,800 ГГц @ 950 мВ, Mid 2,354 ГГц @ 1000 мВ, Prime 2,463 ГГц @ 956 мВ) и «Режим GT» (`3`: без лимитов `2000 / 2600 / 3000 МГц`). Задержки снижения частот оптимизированы под 20–30 мс, добавлена защита sysfs от сброса `rate_limit_us` в 1000 со стороны PowerHAL и `uclamp_ctrl`.

#### 3. Гармонический таймер планировщика (CONFIG_HZ=360)
* **Что сделано**: частота системного таймера настроена на 360 Гц (`CONFIG_HZ_360=y`, `CONFIG_HZ=360`).
* **Обоснование**: квант таймера равен $1000 / 360 \approx 2,777\text{ мс}$. Ровно 3 тика укладываются в кадр 8,333 мс (120 Гц) и 6 тиков в кадр 16,666 мс (60 Гц), что полностью исключает фазовый дрейф планировщика относительно дедлайнов vblank SurfaceFlinger и обеспечивает идеальную плавность интерфейса.

#### 4. Микробуст касаний и защита от гонок
* **Что сделано**: усилена логика обработки событий касания в `drivers/input/input.c`.
* **Техническое исправление**: фильтрация ограничена событиями `EV_ABS` с обязательным наличием `BTN_TOUCH` или `INPUT_PROP_DIRECT`, что отсекает шум датчиков положения. Неатомарные проверки времени заменены на атомарный дебаунс 60 мс через `atomic_long_cmpxchg()`. Добавлена интеграция с Oplus EAS через `sysctl_input_boost_enabled = 1` и `sched_assist_input_boost_duration = now + 35 мс`.

#### 5. Сеть по USB (CDC NCM) и устранение Qdisc Use-After-Free
* **Что сделано**: устаревший протокол RNDIS заменён на современный CDC NCM (`CONFIG_USB_CONFIGFS_NCM=y`, `CONFIG_USB_NET_CDC_NCM=y`), устранены паники сетевого гаджета.
* **Техническое исправление**: регистрация модулей в `drivers/usb/gadget/function/f_ncm.c` объединена в единую точку с безопасным откатом. В `drivers/usb/gadget/function/u_ether.c` ликвидирован Qdisc `dev_requeue_skb` Use-After-Free: при переполнении очереди пакет освобождается с инкрементом `tx_dropped` и возвратом `NETDEV_TX_OK` вместо `NETDEV_TX_BUSY`. Остановка таймера `hrtimer_cancel` теперь строго предшествует освобождению интерфейса netdev.

#### 6. Стелс и VFS: стабилизация подсистемы SuSFS v1.5.5
* **Что сделано**: интегрирована и стабилизирована подсистема маскировки SuSFS 1.5.5 на 32 файлах ядра.
* **Техническое исправление**: добавлен вызов `drop_super(s)` на пути возврата `-EINVAL` в `fs/statfs.c:vfs_ustat()`. В `fs/stat.c:generic_fillattr()` все стандартные поля заполняются до проверки маскировки, исключая утечку неинициализированной памяти стека ядра. Хеш-таблицы переведены на RCU (`hash_add_rcu`, `hash_del_rcu`, `kfree_rcu`, `rcu_read_lock`). Вызовы `kern_path` и аллокации `GFP_KERNEL` вынесены из-под спинлоков, а супервызовы `prctl` и `reboot` закрыты проверкой `current_uid().val == 0`.

#### 7. Архитектура рут-доступа: KernelSU-Next v3.4.0-legacy (UAPI 4)
* **Что сделано**: прямая in-tree интеграция драйвера KernelSU-Next v3.4.0-legacy (`CONFIG_KSU_MANUAL_HOOK=y`) с изолированными дескрипторами (`[ksu_driver_su]`).
* **Техническое исправление**: скорректирована валидация корня точки монтирования в `ksu_try_umount()`, что ликвидировало спам ошибок `KernelSU: umount /data/adb/modules failed: -22` при спавне приложений. Установка флага `TASK_STRUCT_NON_ROOT_USER_APP_PROC` ограничена диапазоном `new_uid >= 10000`, исключая клеймение системных демонов. Подключён хук `ksu_handle_slow_avc_audit()` в SELinux AVC.

#### 8. Сеть, файловые системы и совместимость с Google VINTF FCM Level 5
* **Что сделано**: алгоритм контроля перегрузки TCP по умолчанию возвращён на **Cubic** (`CONFIG_DEFAULT_TCP_CONG="cubic"`) в паре с планировщиком **FQ-CoDel** (`CONFIG_DEFAULT_NET_SCH="fq_codel"`).
* **Техническое исправление**: модуль NFS строго отключён (`# CONFIG_NFS_FS is not set`), что гарантирует 100% прохождение матрицы совместимости Google Android 13 VINTF FCM Level 5 и исключает появление системного диалога об ошибке. Включена поддержка сетевых дисков SMB3/CIFS на уровне ядра (`CONFIG_NETWORK_FILESYSTEMS=y`, `CONFIG_CIFS=y`). Глубина очередей запросов UFS 3.1 увеличена до 256.

#### 9. Модуль-компаньон и автоматизация userspace
* **Что сделано**: модуль `rmx3357-neocore-companion` синхронизирован с ядерными механизмами Ring 0.
* **Техническое исправление**: в `power_sync.sh` добавлена маска `:cy` (`close_write` / `moved_to`) для утилиты `inotifyd`, снизившая потребление CPU с 4–5% до **0.0%** и остановившая петлю повторного чтения настроек. Создан файл `system.prop` для пре-Zygote инициализации кучи ART Dalvik VM. Миграция потоков Bluetooth LDAC переписана на встроенные операции оболочки без подпроцессов, синхронизированы оба узла запаса частоты GED GPU DVFS.
