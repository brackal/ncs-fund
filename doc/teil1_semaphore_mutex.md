# Teil 1: Semaphore und Mutex

## Semaphore

### Grundprinzip

Ein Semaphore ist im Kern ein **Zähler**, der signalisiert, wie viele Instanzen einer gemeinsam genutzten Ressource gerade verfügbar sind. Es ist ein reiner **Signalisierungsmechanismus**, kein Datencontainer.

**Anschauliches Bild:** Ein Parkhaus mit begrenzten Stellplätzen. Eine Anzeige zeigt "noch X frei". Fährt ein Auto rein → Zähler sinkt ("Take"). Fährt ein Auto raus → Zähler steigt ("Give"). Ist der Zähler bei 0, muss das nächste Auto warten.

**Historische Analogie – Eisenbahnsignal:** Der Name stammt direkt von der Bahn-Signaltechnik. Ein Gleisabschnitt darf nur von begrenzt vielen Zügen gleichzeitig befahren werden. Wichtig: Nicht der Zug selbst schaltet das Signal zurück, sondern das Stellwerk (eine externe Instanz) – das entspricht genau der fehlenden Ownership beim Semaphore.

### Eigenschaften

- **Initialisierung**: Startwert (≥ 0) und Maximum werden festgelegt: `K_SEM_DEFINE(sem, initial_count, max_count)`.
- **„Give"**: erhöht den Zähler um 1 (bis maximal `max_count`). Darf **aus Thread oder ISR** aufgerufen werden.
- **„Take"**: verringert den Zähler um 1. Ist der Zähler 0, **blockiert** der aufrufende Thread, bis jemand anderes gibt. Nur aus **Thread-Kontext** sinnvoll nutzbar (nicht wartend aus ISR).
- **Keine Ownership**: Wer nimmt, muss nicht derjenige sein, der gibt – jeder Thread/jede ISR darf geben, unabhängig davon, wer genommen hat.
- **Keine Priority Inheritance**: Da es keinen festen "Besitzer" gibt, kann das System keine Priorität "verleihen".

### Zustandsdiagramm (Available / Unavailable)

- **Available** (count > 0): mindestens eine Instanz frei.
- **Unavailable** (count = 0): nichts frei, wartende Threads werden blockiert.
- Übergang Available → Unavailable: `Take` bei count == 1 → count wird 0.
- Übergang Unavailable → Available: `Give` bei count == 0 → count wird 1, wartender Thread wird beim nächsten Reschedule-Punkt entblockt.

### Wartendes Verhalten

Ruft ein Thread `k_sem_take()` auf und ist nichts verfügbar, wird er **komplett blockiert** (Zustand "pending") – er verbraucht in dieser Zeit **keine CPU-Zeit** (im Gegensatz zu aktivem Busy-Waiting). Der Scheduler gibt die CPU an andere Threads/den Idle-Thread.

**Risiko des ewigen Wartens:** Kann eintreten durch vergessenes `give()`, Deadlocks (zirkuläres Warten zweier Threads) oder einen hängenden "Geber". Abhilfe: Timeout statt `K_FOREVER`:

```c
int ret = k_sem_take(&work_done_sem, K_MSEC(500));
if (ret == 0) {
    // erfolgreich erhalten
} else if (ret == -EAGAIN) {
    // Timeout abgelaufen
}
```

Timeout-Optionen: `K_FOREVER` (unbegrenzt warten), `K_NO_WAIT` (sofort zurück), `K_MSEC(n)`/`K_SECONDS(n)` (begrenzt warten).

### Beispiel 1: Signalisierung (Producer-Consumer, max = 1)

```c
K_SEM_DEFINE(work_done_sem, 0, 1);   // Start: 0 (nichts fertig), Max: 1

void offload_function(struct k_work *work_item) {
    emulate_work();
    k_sem_give(&work_done_sem);      // "Fertig!"
}

void thread0(void) {
    k_work_submit_to_queue(&offload_work_q, &my_work.work);
    k_sem_take(&work_done_sem, K_FOREVER);  // wartet, bis wirklich fertig
}
```
Startet im Zustand **Unavailable** (0,1) – niemand darf durch, bis explizit signalisiert wird. Typisch für Ereignisse zwischen unterschiedlichen Rollen (Producer meldet, Consumer reagiert).

### Beispiel 2: Ressourcen-Pool (mehrere Instanzen, max > 1)

```c
#define NUM_BUFFERS 3
K_SEM_DEFINE(buffer_sem, NUM_BUFFERS, NUM_BUFFERS);  // Start: 3 frei, Max: 3

void worker_thread(void) {
    while (1) {
        k_sem_take(&buffer_sem, K_FOREVER);   // eine der 3 Instanzen belegen
        int idx = find_free_buffer();
        process_data(sensor_buffers[idx]);
        buffer_in_use[idx] = false;
        k_sem_give(&buffer_sem);               // wieder freigeben
    }
}
```
Startet im Zustand **Available** (3,3) – die ersten 3 Threads bekommen sofort eine Instanz, der 4. muss warten.

### `(10,10)` vs. `(0,10)` im Vergleich

| | `(10, 10)` | `(0, 10)` |
|---|---|---|
| Anfangszustand | Available (voll) | Unavailable (leer) |
| Muster | Ressourcen-Pool, sofort nutzbar | Signalisierung, wartet auf Ereignisse |

### Typisches Einsatzgebiet

**Producer-Consumer-Szenarien**: unterschiedliche Rollen (z. B. ISR signalisiert, Thread verarbeitet) oder Verwaltung mehrerer gleichartiger Ressourceninstanzen.

---

## Mutex

### Grundprinzip

Ein Mutex (**MUT**ual **EX**clusion) kennt nur zwei Zustände: **locked** oder **unlocked**. Er dient dem exklusiven Schutz einer **Critical Section** – eines Codeabschnitts, der ohne Unterbrechung durch andere Threads ablaufen muss, damit gemeinsam genutzte Daten nicht korrumpiert werden.

**Bild:** Eine Tür mit genau einem Schlüssel. Ein Thread lockt (nimmt den Schlüssel, schließt ab), andere warten blockiert davor, bis der Thread wieder unlockt (aufschließt).

### Eigenschaften

- **Lock**: erhöht einen internen Lock-Count. Blockiert den aufrufenden Thread, falls ein **anderer** Thread den Mutex bereits hält.
- **Unlock**: verringert den Lock-Count. Bei Count 0 ist der Mutex frei.
- **Ownership**: Nur der Thread, der gelockt hat, darf auch unlocken – im Gegensatz zum Semaphore.
- **Nicht in ISRs nutzbar** (weder lock noch unlock) – ISRs können nicht am Ownership-/Priority-Inheritance-Mechanismus teilnehmen und dürfen nicht blockieren.
- **Priority Inheritance**: Da der Besitzer klar ist, kann das System ihm bei Bedarf vorübergehend eine höhere Priorität leihen, um Priority Inversion zu vermeiden (ein niedrig priorisierter Halter würde sonst durch mittelpriorisierte Threads ausgebremst, während ein hochpriorisierter Thread wartet).

### Was ist eine Critical Section?

Ein **Stück Code** (keine Daten/kein Speicherbereich!), das exklusiv, ohne Unterbrechung durch andere Threads laufen muss. Beispiel für eine nicht-atomare Operation, die eine Critical Section braucht:

```c
counter = counter + 1;   // sieht atomar aus, ist aber: laden, erhöhen, zurückschreiben
```

Mehrere verschiedene Code-Stellen, die dieselbe Ressource anfassen, sind jeweils eigene Critical Sections – müssen aber denselben Schutzmechanismus (denselben Mutex) verwenden.

### Mutex-Objekt und geschützte Daten sind getrennt

```c
K_MUTEX_DEFINE(buffer_array_mutex);   // das Schloss
bool buffer_in_use[NUM_BUFFERS];       // die Daten
```

Der Mutex "kennt" die Daten nicht technisch – die Verbindung ist reine **Programmier-Disziplin**: Nur wenn *jede* Stelle, die `buffer_in_use[]` anfasst, konsequent denselben Mutex verwendet, ist der Schutz wirksam.

### Beispiel: Schutz eines gemeinsamen Arrays

```c
K_MUTEX_DEFINE(buffer_array_mutex);
bool buffer_in_use[NUM_BUFFERS];

int find_free_buffer(void) {
    k_mutex_lock(&buffer_array_mutex, K_FOREVER);
    int idx = -1;
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (!buffer_in_use[i]) {
            buffer_in_use[i] = true;
            idx = i;
            break;
        }
    }
    k_mutex_unlock(&buffer_array_mutex);
    return idx;
}
```

### Rückgabewert von `k_mutex_lock()`

```c
int ret = k_mutex_lock(&buffer_array_mutex, K_MSEC(100));
```

| Rückgabewert | Bedeutung |
|---|---|
| `0` | erfolgreich gelockt |
| `-EBUSY` | belegt, bei `K_NO_WAIT` sofort zurückgegeben |
| `-EAGAIN` | Timeout abgelaufen |

Prüfen und Locken passieren **atomar in einem Schritt** – kein separates Vorab-Prüfen nötig oder sinnvoll.

### Recursive (Reentrant) Locking

Derselbe Thread darf denselben Mutex **mehrfach** hintereinander locken, ohne sich selbst zu blockieren (nützlich bei verschachtelten Funktionsaufrufen):

```c
void inner_function(void) {
    k_mutex_lock(&my_mutex, K_FOREVER);   // 2. Lock, kein Blockieren (gleicher Thread)
    k_mutex_unlock(&my_mutex);
}

void outer_function(void) {
    k_mutex_lock(&my_mutex, K_FOREVER);   // 1. Lock
    inner_function();
    k_mutex_unlock(&my_mutex);             // muss genauso oft unlocken wie gelockt!
}
```

Wichtig: Anzahl `lock()`- und `unlock()`-Aufrufe muss übereinstimmen, sonst bleibt der Mutex für andere Threads dauerhaft blockiert. Ein binärer Semaphore hätte sich hier stattdessen selbst deadlocked.

### Typisches Einsatzgebiet

**Mutual-Exclusion-Szenarien**: Schutz gemeinsam genutzter Ressourcen/Variablen vor gleichzeitigem Zugriff durch mehrere (oft gleichartige) Threads, z. B.:
- Schutz globaler/geteilter Variablen oder Arrays
- Gemeinsam genutzte Hardware-Peripherie (z. B. I2C/SPI-Bus)
- Konsistenz mehrerer zusammengehöriger Datenfelder (Struct-Updates)

### Semaphore vs. Mutex im Überblick

| Eigenschaft | Semaphore | Mutex |
|---|---|---|
| Werte | 0 bis MAX (mehrere Instanzen) | nur locked/unlocked (binär) |
| Ownership | keine | ja – nur der Locker darf unlocken |
| Priority Inheritance | nein | ja |
| Nutzbar in ISR | ja (nur `give()`) | nein (weder lock noch unlock) |
| Recursive Locking | führt zu Selbst-Deadlock | funktioniert (Lock-Count) |
| Typischer Einsatz | Ressourcen-Pool, Signalisierung zwischen Rollen | Schutz einer Critical Section innerhalb gleichartiger Zugriffe |
