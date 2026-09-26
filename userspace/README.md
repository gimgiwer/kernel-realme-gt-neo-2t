# Realme GT Neo 2T (RMX3357) Userspace Scripts & Module

Companion components for **KernelSU-Next** or Magisk on Android 13 / realme UI 4.0 (`RMX3357_13.1.0.500(CN01)`). Version: **v2026.09.26**.

[English](#english) | [Русский](#русский)

---

<a name="english"></a>
## English

### Directory Structure

```text
userspace/
├── module/                  # KernelSU / Magisk module
│   ├── META-INF/...
│   ├── module.prop          # Metadata and update channel
│   ├── system.prop          # Pre-Zygote ART Dalvik VM heap properties
│   ├── service.sh           # Post-boot tuning service
│   └── power_sync.sh        # Power profile sync daemon
└── service.d/               # Standalone scripts for manual install
    ├── 00_system_service.sh
    └── 99_power_profile_sync.sh
```

### Components

#### `service.sh` (`service.d/00_system_service.sh`) & `system.prop`

`system.prop` applies ART Dalvik VM heap bounds (`dalvik.vm.heapstartsize=12m`, `dalvik.vm.heapgrowthlimit=512m`, `dalvik.vm.heapsize=768m`, `dalvik.vm.heaptargetutilization=0.70`, `dalvik.vm.heapminfree=12m`, `dalvik.vm.heapmaxfree=48m`) via KernelSU `resetprop` prior to `zygote64` initialization. `service.sh` runs once after `/data` decryption and `sys.boot_completed=1`:

*   **Telemetry Cleanup**: Stops MediaTek debug loggers (`emdlogger`, `connsyslogger`, `mobile_log_d`, `bt_dump`, `wifi_dump`, `netdiag`) and ColorOS crash uploaders (`criticallog`, `common_dcs`, `phoenix_log_manager`). Keeps `oiface` alive for gaming GPU DVFS.
*   **SurfaceFlinger Tuning**: Utilizes native ColorOS 13.1 dynamic duration engine (`vendor.debug.sf.dynamic_duration.*`) and removes legacy static overrides.
*   **ART Runtime Bounds**: Configures Dalvik heap sizes proportional to physical RAM, preventing GC stutter and early background process eviction.
*   **Schedutil & GED GPU DVFS**: Manages cluster frequency caps and transition rate limits via `/proc/sys/kernel/sched_power_profile`, and sets GED GPU DVFS headroom via `/sys/kernel/ged/hal/dvfs_margin_value` (`15`) and `/sys/module/ged/parameters/gx_fb_dvfs_margin` (`150`).
*   **Cpusets & UFS 3.1 Storage Queue Tuning**: Allocates top-app tasks to Prime/Mid cores (`0-7`), confines background work to Little cores (`0-3`), and tunes UFS 3.1 `sd*` block queues (`cfq` scheduler, `nr_requests=256`, `read_ahead_kb=256`, `rq_affinity=1`, `nomerges=0`, `add_random=0`).
*   **TCP Network Stack**: Enforces `tcp_congestion_control=cubic`, enables `tcp_fastopen=3`, sets `tcp_ecn=2` to prevent carrier CGNAT blackholing, and disables `tcp_slow_start_after_idle` and `tcp_autocorking`.
*   **Three-Layer Bluetooth LDAC Little-Core Isolation**: Enforces strict confinement of high-bitrate LDAC audio processing to Cortex-A55 Little cores (0–3) using fork-free shell built-ins (`read -r comm < "$task/comm"`, `${task##*/}`) and `cgroup.procs`:
    *   *Layer 1 (Audio Stack & Encoder)*: Pins `com.android.bluetooth` threads (`bt_a2dp_source_` [LDAC encoder], `bt_stack_manage`, `BluetoothA2dpLo`, `bta_media_*`) to `/dev/cpuset/system-background/tasks` and cores 0–3 (`taskset -p 0f`).
    *   *Layer 2 (Vendor HAL)*: Migrates MediaTek Bluetooth HAL (`android.hardware.bluetooth@1.1-service-mediatek`) to `/dev/cpuset/system-background/cgroup.procs` and pins all worker threads to Little cores (`taskset -p 0f`).
    *   *Layer 3 (ARM GICv3 Discrete IRQ Affinity)*: Eliminates 1-of-N routing interrupt leakage from composite mask `0f` (~23.7% leakage to Cortex-A78 big cores) by mapping IRQs with discrete bitwise masks strictly to Cortex-A55 Little cores:
        - IRQ 18 (`mtk btif tx dma irq`) -> Core 0 (`smp_affinity: 01`)
        - IRQ 19 (`mtk btif rx dma irq`) -> Core 1 (`smp_affinity: 02`)
        - IRQ 228 (`BTIF_WAKEUP_IRQ`) -> Core 2 (`smp_affinity: 04`)
        - IRQ 17 (`mtk btif irq`) & IRQ 281 (`BTCVSD_ISR_Handle`) -> Core 3 (`smp_affinity: 08`)
        - *Verification*: Cores 4–7 maintain strictly +0 interrupts (+0% leak), ensuring 100% hardware isolation of Cortex-A78 in Deep Sleep.
    *   *Power & Thermals*: LDAC power drain reduced 4.5–5× (from 18–24%/hr down to 3.5–5%/hr), keeping Little cores at 600 MHz and ambient 26.2 °C.

#### `power_sync.sh` (`service.d/99_power_profile_sync.sh`)

Event-driven daemon synchronizing ColorOS power toggles and Bluetooth connection state with NeoCore's kernel scheduler profiles (`/proc/sys/kernel/sched_power_profile`):

*   **Why Schedutil Synchronization is Needed**: The stock Dimensity 1200 CPU governor aggressively overboosts Cortex-A78 cores up to 2.6–3.0 GHz even for simple UI scrolling or background tasks, pulling up to ~3 W active CPU power and inducing rapid thermal throttling. Stock ColorOS power-saving modes either freeze display fluidness or aggressively hotplug cores (causing noticeable microstutter). The daemon pairs with NeoCore's lockless in-kernel frequency ceilings to enforce optimal hardware OPP limits per cluster without frame drops or polling overhead.
*   **100% Event-Driven Architecture (`:cy` Filter)**: Fully asynchronous daemon driven by filesystem events via Toybox `inotifyd - "$TARGET_DIR:cy" "$BT_STATE_DIR:cy"` across `/data/system/users/0` (`settings_system.xml`, `settings_global.xml`) and `/data/misc/bluetooth` (`cached_hci*`, `*bt*`, `*blue*`). Filtering strictly on `close_write` (`c`) and `moved_to` (`y`) prevents read-loop self-amplification when querying `settings`. In idle state, the daemon blocks in kernel `read()` consuming 0.0% CPU, zero child page faults (`cminflt`), and zero periodic timer wakeups, preserving hardware Deep Sleep.
*   **Power Profiles (Sequential Order 0–3)**:
    *   `0` (**Super Power Saving**): Triggered by ColorOS Super Power Saving mode (`super_powersave_mode_state=1`). Cuts power to the `VPROC2` rail by offlining Mid cores (4–6) via MediaTek PPM (`/proc/ppm/policy/forcelimit_cpu_core`). Operates in dynamic **4 Little + 1 Prime** topology (`1625 / Offline / 1998 MHz`) with a 10% GPU DVFS margin (`10` in `/sys/kernel/ged/hal/dvfs_margin_value`, `100` in `/sys/module/ged/parameters/gx_fb_dvfs_margin`) and 66 mA idle drain.
    *   `1` (**Power Saving**): Triggered by standard Battery Saver (`low_power=1`). Keeps all 8 cores online with energy-efficient frequency ceilings (`1625 / 1985 / 2141 MHz`, `-20.6%` active power vs Balanced) and a 10% GPU DVFS margin, eliminating hotplug lag while keeping 120 Hz display fluidity.
    *   `2` (**Balanced**): Default daily profile (`1800 / 2354 / 2463 MHz`). Operates at the physical energy-efficiency optimum of the MT6893 TSMC N6 process with a 15% GPU DVFS margin (`15` in `/sys/kernel/ged/hal/dvfs_margin_value`, `150` in `/sys/module/ged/parameters/gx_fb_dvfs_margin`). Sustains zero thermal buildup during daily multitasking.
    *   `3` (**GT Mode / Gaming**): Triggered by the ColorOS GT Mode Quick Settings tile (`gt_mode_state_setting=1`) or game space. Unlocks all cluster caps (`2000 / 2600 / 3000 MHz`) with a 15% GPU DVFS margin for maximum compute and gaming performance.

### Binary Compatibility (KABI)

The companion module operates purely in userspace and maintains full compatibility with closed vendor blobs and existing root solutions.

### Installation

#### Option 1: Module (Recommended)

Flash `rmx3357-neocore-companion.zip` via KernelSU Manager or Magisk (automatically removes any legacy standalone `/data/adb/service.d/00_system_service.sh` and `99_power_profile_sync.sh` scripts).

#### Option 2: Manual Setup

```sh
mkdir -p /data/adb/service.d

cp userspace/service.d/00_system_service.sh /data/adb/service.d/
cp userspace/service.d/99_power_profile_sync.sh /data/adb/service.d/

chmod 755 /data/adb/service.d/00_system_service.sh \
          /data/adb/service.d/99_power_profile_sync.sh
chown -R root:root /data/adb/service.d
```

---

<a name="русский"></a>
## Русский

### Структура каталогов

```text
userspace/
├── module/                  # Модуль KernelSU / Magisk
│   ├── META-INF/...
│   ├── module.prop          # Метаданные и канал обновлений
│   ├── system.prop          # Свойства кучи ART Dalvik VM (до старта Zygote)
│   ├── service.sh           # Сервис настройки после загрузки
│   └── power_sync.sh        # Демон синхронизации профилей питания
└── service.d/               # Скрипты для ручной установки
    ├── 00_system_service.sh
    └── 99_power_profile_sync.sh
```

### Компоненты

#### `service.sh` (`service.d/00_system_service.sh`) и `system.prop`

Файл `system.prop` применяет параметры кучи ART (`dalvik.vm.heapstartsize=12m`, `dalvik.vm.heapgrowthlimit=512m`, `dalvik.vm.heapsize=768m`, `dalvik.vm.heaptargetutilization=0.70`, `dalvik.vm.heapminfree=12m`, `dalvik.vm.heapmaxfree=48m`) через `resetprop` в KernelSU ещё до инициализации `zygote64`. Скрипт `service.sh` выполняется однократно после расшифровки `/data` и установки флага `sys.boot_completed=1`:

*   **Очистка от телеметрии**: Останавливает фоновые дамперы MediaTek (`emdlogger`, `connsyslogger`, `mobile_log_d`, `bt_dump`, `wifi_dump`, `netdiag`) и службы сбора отчётов ColorOS (`criticallog`, `common_dcs`, `phoenix_log_manager`). Сохраняет `oiface` активным для работы игрового GPU DVFS.
*   **Настройка SurfaceFlinger**: Задействует штатный движок динамической длительности ColorOS 13.1 (`vendor.debug.sf.dynamic_duration.*`) и убирает устаревшие статические задержки.
*   **Границы кучи ART**: Выставляет размеры кучи Dalvik пропорционально объёму физической памяти при загрузке, предотвращая микрофризы сборщика мусора и раннюю выгрузку фоновых приложений.
*   **Говернор Schedutil и GED GPU DVFS**: Управляет частотными лимитами кластеров и задержками переключения через ядерный узел `/proc/sys/kernel/sched_power_profile`, а также задаёт порог запаса GPU DVFS через `/sys/kernel/ged/hal/dvfs_margin_value` (`15`) и `/sys/module/ged/parameters/gx_fb_dvfs_margin` (`150`).
*   **Cpusets и настройка очередей UFS 3.1**: Выделяет ядра Prime/Mid (`0-7`) под активные приложения, изолирует фоновые процессы на ядрах Little (`0-3`) и настраивает очереди блочных устройств UFS 3.1 `sd*` (планировщик `cfq`, `nr_requests=256`, `read_ahead_kb=256`, `rq_affinity=1`, `nomerges=0`, `add_random=0`).
*   **Сетевой стек TCP**: Устанавливает алгоритм контроля перегрузки `tcp_congestion_control=cubic`, включает `tcp_fastopen=3`, переводит ECN в надёжный режим ответа `tcp_ecn=2` для защиты от сброса соединений в CGNAT операторов и отключает `tcp_slow_start_after_idle` и `tcp_autocorking`.
*   **Трёхуровневая аппаратная изоляция Bluetooth LDAC на малых ядрах Cortex-A55 (ядра 0–3)**: Выполняется без порождения лишних подпроцессов за счёт встроенных операций чтения оболочки (`read -r comm < "$task/comm"`, `${task##*/}`) и пакетной миграции через `cgroup.procs`:
    *   *Уровень 1 (Потоки кодека и аудиостека)*: Потоки процесса `com.android.bluetooth` (`bt_a2dp_source_` [LDAC encoder], `bt_stack_manage`, `BluetoothA2dpLo`, `bta_media_*`) заблокированы на ядра 0–3 через `/dev/cpuset/system-background/tasks` и `taskset -p 0f`.
    *   *Уровень 2 (Вендорный сервис HAL)*: Служба MediaTek Bluetooth Audio HAL (`android.hardware.bluetooth@1.1-service-mediatek`) переведена в `/dev/cpuset/system-background/cgroup.procs`, а все её рабочие потоки привязаны к ядрам 0–3 через `taskset -p 0f`.
    *   *Уровень 3 (Аппаратная маршрутизация прерываний ARM GICv3)*: Устранена утечка прерываний при составной маске `0f` (где из-за механизма 1-of-N routing контроллера GICv3 до ~23.7% прерываний просачивались на производительные ядра Cortex-A78). Прерывания разведены побитово на конкретные малые ядра:
        - IRQ 18 (`mtk btif tx dma irq`) -> ядро 0 (`smp_affinity: 01`)
        - IRQ 19 (`mtk btif rx dma irq`) -> ядро 1 (`smp_affinity: 02`)
        - IRQ 228 (`BTIF_WAKEUP_IRQ`) -> ядро 2 (`smp_affinity: 04`)
        - IRQ 17 (`mtk btif irq`) и IRQ 281 (`BTCVSD_ISR_Handle`) -> ядро 3 (`smp_affinity: 08`)
        - *Аппаратный замер*: На ядрах 4–7 инкремент прерываний строго равен +0 (+0%), достигнута 100% аппаратная изоляция кластера A78 в Deep Sleep.
    *   *Эффект на аккумуляторе*: Разряд при прослушивании Hi-Res аудио по кодеку LDAC снизился в 4.5–5 раз (с 18–24%/ч до 3.5–5%/ч). Частота кластера Little опустилась до базовых 600 МГц при минимальном напряжении шины. Температура устройства держится на комнатных 26.2 °C.

#### `power_sync.sh` (`service.d/99_power_profile_sync.sh`)

Событийный демон, синхронизирующий системные переключатели питания ColorOS и статус подключения Bluetooth с планировщиком ядра (`/proc/sys/kernel/sched_power_profile`):

*   **Зачем нужна синхронизация планировщика**: На стоковом ядре Dimensity 1200 планировщик `schedutil` чрезмерно агрессивно задирает частоты производительных ядер Cortex-A78 до 2,6–3,0 ГГц даже при элементарном скролле списков, вызывая скачки энергопотребления до ~3 Вт на CPU, быстрый нагрев и температурный троттлинг. Штатное энергосбережение ColorOS либо ломает плавность 120 Гц, либо грубо отключает ядра с микрофризами хотплага. Демон работает в связке с энергоэффективными профилями ядра, мгновенно переключая аппаратные частотные потолки без задержек и без поллинга.
*   **100% Event-Driven архитектура (фильтр `:cy`)**: Полностью асинхронный событийный демон на базе Toybox `inotifyd - "$TARGET_DIR:cy" "$BT_STATE_DIR:cy"`, отслеживающий запись и атомарное перемещение файлов (`close_write` / `moved_to`) в `/data/system/users/0` (`settings_system.xml`, `settings_global.xml`) и `/data/misc/bluetooth` (`cached_hci*`, `*bt*`, `*blue*`). Фильтрация `:cy` полностью исключает паразитную петлю самовозбуждения при чтении настроек через `settings get`. В простое процесс блокируется на системном вызове `read()`, потребляет 0.0% процессора, даёт 0 страничных ошибок дочерних процессов (`cminflt`) и делает 0 периодических пробуждений, сохраняя глубокий сон процессора (Deep Sleep).
*   **Профили питания (по порядку 0–3)**:
    *   `0` (**Супер-энергосбережение**): Включается при активации режима «Суперэнергосбережение» ColorOS (`super_powersave_mode_state=1`). Обесточивает шину `VPROC2` путём отключения средних ядер (4–6) через MediaTek PPM (`/proc/ppm/policy/forcelimit_cpu_core`). Работает в динамической топологии **4 Little + 1 Prime** (`1625 / Отключены / 1998 МГц`) с запасом GPU DVFS 10% (`10` в `/sys/kernel/ged/hal/dvfs_margin_value`, `100` в `/sys/module/ged/parameters/gx_fb_dvfs_margin`) и током покоя 66 мА.
    *   `1` (**Энергосбережение**): Включается при активации стандартного энергосбережения (`low_power=1`). Сохраняет активность всех 8 ядер с пониженными энергоэффективными частотными потолками (`1625 / 1985 / 2141 МГц`, `-20,6%` мощности к Балансу) и запасом GPU DVFS 10% без задержек хотплага и с полной плавностью 120 Гц.
    *   `2` (**Сбалансированный**): Повседневный профиль по умолчанию (`1800 / 2354 / 2463 МГц`). Работает в точке физического оптимума энергоэффективности чипа MT6893 на техпроцессе TSMC N6 с запасом GPU DVFS в 15% (`15` в `/sys/kernel/ged/hal/dvfs_margin_value`, `150` в `/sys/module/ged/parameters/gx_fb_dvfs_margin`). Обеспечивает холодный корпус при любых повседневных задачах.
    *   `3` (**GT Mode / Игры**): Активируется плиткой режима GT в шторке (`gt_mode_state_setting=1`) или игровым пространством. Снимает частотные лимиты со всех кластеров (`2000 / 2600 / 3000 МГц`) при запасе GPU DVFS 15% для максимальной производительности в тяжёлых вычислениях и играх.

### Бинарная совместимость (KABI)

Компоненты модуля работают исключительно в пространстве пользователя и полностью совместимы с закрытыми бинарными блобами вендора и менеджерами рут-доступа.

### Установка

#### Вариант 1: Модуль (рекомендуется)

Прошейте архив `rmx3357-neocore-companion.zip` через KernelSU Manager или Magisk (при установке автоматически удаляются дублирующие автономные скрипты `/data/adb/service.d/00_system_service.sh` и `99_power_profile_sync.sh`).

#### Вариант 2: Ручная установка

```sh
mkdir -p /data/adb/service.d

cp userspace/service.d/00_system_service.sh /data/adb/service.d/
cp userspace/service.d/99_power_profile_sync.sh /data/adb/service.d/

chmod 755 /data/adb/service.d/00_system_service.sh \
          /data/adb/service.d/99_power_profile_sync.sh
chown -R root:root /data/adb/service.d
```
