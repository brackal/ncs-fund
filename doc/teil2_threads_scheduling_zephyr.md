# Teil 2: Threads, Scheduling und Zephyr-Konfiguration

## Multitasking vs. Multithreading

- **Multitasking**: Das Betriebssystem führt mehrere **Prozesse** (quasi-)gleichzeitig aus. Jeder Prozess hat einen eigenen, isolierten Speicherbereich.
- **Multithreading**: Ein einzelner Prozess wird in mehrere **Threads** unterteilt, die sich denselben Speicherbereich teilen (außer dem Stack – siehe unten).
- In der Embedded/RTOS-Welt (Zephyr, FreeRTOS) gibt es meist keine echte Prozess-Isolation. Was Zephyr "Thread" nennt, ist technisch gesehen **Multithreading**, wird umgangssprachlich aber oft "Multitasking" genannt (historisch bedingt durch den Begriff "Task" in älteren RTOS wie FreeRTOS/VxWorks).

## nRF52840 und Multitasking/Multithreading

- Der nRF52840 hat einen **einzelnen ARM Cortex-M4F Kern** – von Haus aus kein Multitasking ohne Betriebssystem.
- Durch Hardware-Features (SysTick-Timer, NVIC) kann ein **RTOS** (Zephyr, FreeRTOS) pseudo-paralleles Multitasking/Multithreading realisieren.
- Zephyr verwendet konsequent den Begriff **"Thread"**, nicht "Task" – alle Threads teilen sich denselben Adressraum (kein MMU-Prozessschutz im Standardfall).

## `K_THREAD_DEFINE` und Thread-IDs

```c
K_THREAD_DEFINE(thread0_id, STACKSIZE, thread0, NULL, NULL, NULL, THREAD0_PRIORITY, 0, 0);
```

- `thread0_id` ist ein **frei wählbarer Bezeichner**, kein reservierter Name.
- Datentyp: **`k_tid_t`** (= `struct k_thread *`, ein Pointer/Handle auf die Thread-Struktur).
- Das Makro legt im Hintergrund an: einen Stack-Bereich, eine `struct k_thread`-Instanz, und die `k_tid_t`-Konstante, die darauf zeigt.

## Yield

- `k_yield()`: Ein Thread gibt **freiwillig** die CPU ab, bleibt aber "ready" (kein Blockieren).
- Betrifft nur **gleich priorisierte** Threads – die stellen sich hinten in die Warteschlange.
- Besonders wichtig bei **kooperativen Prioritäten** (negative Werte in Zephyr), da diese Threads sonst die CPU nie freiwillig abgeben würden.
- Unterschied zu `k_sleep()`: Yield kehrt sofort wieder in den Ready-Zustand zurück (kein Timer), Sleep blockiert für eine definierte Zeit.

## Timeslicing

```c
CONFIG_TIMESLICING=y
CONFIG_TIMESLICE_SIZE=10       // ms pro Zeitscheibe
CONFIG_TIMESLICE_PRIORITY=5    // Schwelle: nur Threads mit Prio >= 5 werden timesliced
```

- Threads mit **gleicher Priorität** teilen sich die CPU fair per Zeitscheibe.
- `CONFIG_TIMESLICE_PRIORITY` legt eine Schwelle fest: Nur Threads mit numerisch **gleicher oder höherer** Prioritätszahl (= niedrigere Dringlichkeit) werden von Timeslicing erfasst. Wichtige, sehr hoch priorisierte Threads können so bewusst ausgeschlossen werden.
- Threads mit **höherer** Priorität verdrängen (preemption) niedriger priorisierte **sofort** – unabhängig von Timeslicing.

## `k_busy_wait()` und Preemption

- `k_busy_wait(us)` ist eine Spin-Loop, die gegen einen **unabhängig laufenden Hardware-Timer** prüft.
- Wird der Thread währenddessen preemptet, läuft der Hardware-Zähler im Hintergrund weiter – die Gesamtwartezeit (Wanduhrzeit) bleibt dadurch ungefähr korrekt, auch über mehrere Unterbrechungen hinweg.
- Antipattern für Dauerbetrieb: verbrennt aktiv CPU-Zyklen. Besser: `k_msleep()`, das den Thread wirklich blockiert (CPU wird frei für andere/Idle).

## Threads anzeigen

1. **Shell**: `CONFIG_SHELL=y`, `CONFIG_KERNEL_SHELL=y` → Befehl `kernel threads` zeigt Priorität, Status, Stack-Nutzung.
2. **Thread Analyzer**: `CONFIG_THREAD_ANALYZER=y` + `thread_analyzer_print()` im Code → zeigt zusätzlich CPU-Auslastung pro Thread.
3. **Programmatisch**: `k_thread_foreach(callback, NULL)` iteriert über alle Threads zur Laufzeit.
4. Thread-Namen lassen sich per `k_thread_name_set()` setzen (bei `K_THREAD_DEFINE` automatisch über den Symbolnamen sichtbar, bei `k_work_queue_start()` **nicht** automatisch – muss manuell gesetzt werden).

## UART-Konfiguration für die Shell

Notwendige Kconfig-Optionen:
```properties
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_UART_INTERRUPT_DRIVEN=y   # wird implizit durch CONFIG_SHELL mitgezogen (select-Mechanismus)
CONFIG_SHELL=y
CONFIG_KERNEL_SHELL=y
```

Zusätzlich im Devicetree (`chosen`-Knoten):
```dts
chosen {
    zephyr,console = &uart0;
    zephyr,shell-uart = &uart0;
};
```

**Shell-Thread-Priorität explizit setzen:**
```properties
CONFIG_SHELL_THREAD_PRIORITY_OVERRIDE=y   # Schalter: manuelle Priorität erlauben
CONFIG_SHELL_THREAD_PRIORITY=0             # eigentlicher Wert (nur wirksam mit Override=y)
```

**Debugging-Tipps bei fehlender Shell-Ausgabe:**
- `grep -i shell build/zephyr/.config` prüfen, ob Optionen tatsächlich im Build gelandet sind.
- Pristine Rebuild erzwingen: `west build -b <board> . -p always`.
- Prüfen, ob `prj.conf` korrekt benannt und im Projekt-Root liegt.
- Terminal-Programm: richtige Baudrate (meist 115200), Enter drücken für Prompt.

## Virtual COM Port vs. Interface-MCU

- Viele Nordic Dev-Boards (z. B. nRF52840 DK) haben **zwei Chips**: den Ziel-Chip (nRF52840) und einen **Interface-MCU** (Debugger + USB-UART-Bridge).
- Ablauf: `PC (USB) ↔ Interface-MCU ↔ UART-Pins ↔ Ziel-Chip`. Der PC sieht dabei einen **virtuellen COM-Port (VCOM)**.
- Alternative: nativer USB-Controller direkt im nRF52840 (CDC-ACM), ohne Interface-MCU-Umweg – erfordert eigene Konfiguration (`CONFIG_USB_DEVICE_STACK` etc.).
- `printk()` schreibt nur auf **eine** konfigurierte Konsole gleichzeitig. Für parallele Ausgabe auf mehreren Backends: Zephyr **Logging-Subsystem** (`LOG_INF()` etc.) mit mehreren Backends (`CONFIG_LOG_BACKEND_UART`, `CONFIG_LOG_BACKEND_USB`).

## Priorität und Preemption – Praxisbeispiel (thread0/thread1)

Code-Szenario: `thread0` (Prio 2) und `thread1` (Prio 3) führen beide `emulate_work()` (feste Rechenlast) aus, gefolgt von `k_msleep(20)`.

**Beobachtung:** `delta_time` (gemessen mit `k_uptime_get()`/`k_uptime_delta()`) misst **Wanduhrzeit inklusive Unterbrechungen**, nicht reine Rechenzeit:
- thread0 (höchste Prio): läuft ungestört durch, konstante Zeit (~51ms).
- thread1 (niedrigere Prio): wird von thread0 ständig verdrängt, sobald dieser aus dem Sleep aufwacht → deutlich höhere gemessene Zeit (~161ms), obwohl dieselbe Rechenlast.

**Lösung – Workqueue-Offloading:** Die teure Arbeit wird in einen separaten Workqueue-Thread mit **niedrigerer** Priorität ausgelagert:
```c
static K_THREAD_STACK_DEFINE(my_stack_area, WORQ_THREAD_STACK_SIZE);
static struct k_work_q offload_work_q = {0};
struct work_info { struct k_work work; char name[25]; } my_work;

void offload_function(struct k_work *work_item) {
    emulate_work();
}

k_work_queue_start(&offload_work_q, my_stack_area,
                   K_THREAD_STACK_SIZEOF(my_stack_area), WORKQ_PRIORITY, NULL);
k_work_init(&my_work.work, offload_function);
k_work_submit_to_queue(&offload_work_q, &my_work.work);  // nicht-blockierend!
```
Dadurch blockiert thread0 nicht mehr über lange Zeit – thread1 bekommt deutlich mehr faire CPU-Zeit.

**Scheduler-Logik der Workqueue:** Sie läuft nur, wenn kein höher priorisierter Thread ready ist. Da thread0 nach dem Offloading fast durchgehend schläft, läuft die Workqueue praktisch immer dann, wenn thread1 (der jetzt „CPU-hungrigste" Thread) selbst schläft.

**In der Shell-Ausgabe** erscheint der Workqueue-Thread ohne Namen (da nicht über `K_THREAD_DEFINE`, sondern zur Laufzeit über `k_work_queue_start()` erzeugt) – zu unterscheiden von Zephyrs eingebauter `sysworkq` (Prio -1, komplett unabhängig von selbst erstellten Workqueues).

## Stacks

- **Jeder Thread hat einen eigenen, exklusiven Stack** (lokale Variablen, Rücksprungadressen, gesicherte Register bei Kontextwechsel).
- Das ist die Ausnahme zur sonstigen Regel "Threads teilen sich den Speicher" – Heap, globale/statische Variablen und Code-Segment sind geteilt, der Stack nicht.
- Sichtbar in der Shell-Ausgabe (`stack size`, `unused`, `usage`) – wichtig zur Vermeidung von Stack-Overflows.
