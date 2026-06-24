# UART Debug rozhranie

**Projekt:** micronet
**Dokument:** docs/uart_debug.md
**Závislosti:** microsh, microlog

---

## Účel

UART debug rozhranie umožňuje vývojárovi komunikovať s knižnicou počas vývoja a testovania priamo cez sériový port. Je postavené nad `microsh` – má históriu príkazov, tab completion a farebný výstup. V produkčnom build sa dá úplne vykompilovať (`P2P_UART_DEBUG=0`).

---

## Zapojenie

```
ESP32 TX  →  USB-UART adaptér RX  →  PC
ESP32 RX  →  USB-UART adaptér TX  →  PC

Parametre: 115200 baud, 8N1
```

Nástroje na PC:
- Linux/Mac: `screen /dev/ttyUSB0 115200` alebo `minicom`
- Windows: PuTTY, Termite
- VS Code: Serial Monitor rozšírenie

---

## Inicializácia v kóde

```c
#include "micronet.h"
#include "p2p_uart.h"

void app_main(void) {
    p2p_config_t cfg = { ... };
    p2p_init(&cfg);

    // Spustenie UART debug shellu
    p2p_uart_init(115200);

    while (1) {
        p2p_tick();
        p2p_uart_tick();   // spracovanie UART vstupu
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

## Príkazy – prehľad

```
Kategória       Príkaz                    Popis
─────────────────────────────────────────────────────────────
Systém          p2p status                stav knižnice
                p2p version               verzia knižnice
                p2p reboot                reštart zariadenia
                p2p log <level>           zmena log levelu

Uzly            p2p nodes                 zoznam všetkých uzlov
                p2p nodes online          len online uzly
                p2p node <id>             detail uzla
                p2p node trust <id>       reťaz dôvery uzla

Skupiny         p2p groups                zoznam skupín
                p2p group <hash>          detail skupiny
                p2p group create          vytvor novú skupinu
                p2p group invite <id> <hash>  pozvi uzol
                p2p group leave <hash>    odídi zo skupiny

Dáta            p2p vars                  vlastné premenné
                p2p vars <id>             premenné iného uzla
                p2p get <id> <key>        hodnota premennej
                p2p set <key> <value>     nastav vlastnú premennú
                p2p metrics               vlastné metriky
                p2p metrics <id>          metriky iného uzla

Sieť            p2p ping <id>             ping uzla
                p2p send <id> <msg>       pošli správu
                p2p broadcast <hash> <msg> broadcast do skupiny

Diagnostika     p2p packet log on/off     zapni/vypni packet log
                p2p db dump               výpis celej DB
                p2p db sync               vynúť synchronizáciu
                p2p keys                  zobraz vlastné kľúče
```

---

## Príkazy – detailný popis

### `p2p status`

```
p2p> p2p status
──────────────────────────────────────────────────────
micronet v0.1.0
──────────────────────────────────────────────────────
node_id:      4a7b3c8d2e9f1a6b (skrátený)
node_name:    sensor_01
external_ip:  85.160.42.11:51820
state:        ACTIVE
uptime:       1h 23m 45s
nodes:        3 known / 2 online
groups:       2
vars:         4 published
free_heap:    186432 B
log_level:    2 (warn)
──────────────────────────────────────────────────────
```

---

### `p2p nodes`

```
p2p> p2p nodes
──────────────────────────────────────────────────────
 #   node_id          ip               stav     skupina
──────────────────────────────────────────────────────
[0]  4a7b3c8d...  85.160.42.11:51820  ONLINE   home, work
[1]  9f8e7d6c...  82.119.33.77:51820  ONLINE   home
[2]  3b2a1f0e...  91.200.12.44:51820  OFFLINE  home
──────────────────────────────────────────────────────
```

---

### `p2p node <id>`

```
p2p> p2p node 4a7b3c8d
──────────────────────────────────────────────────────
node_id:      4a7b3c8d2e9f1a6b5c0d4e8f3a2b7c1d...
node_name:    display_01
external_ip:  85.160.42.11:51820
stav:         ONLINE
first_seen:   2024-01-15 10:23:44
last_seen:    pred 3s
skupiny:      home, work
invited_by:   9f8e7d6c... (sensor_02)
db_version:   47
──────────────────────────────────────────────────────
```

---

### `p2p node trust <id>`

Zobrazí reťaz dôvery – kto koho pridal:

```
p2p> p2p node trust 3b2a1f0e
──────────────────────────────────────────────────────
Reťaz dôvery pre 3b2a1f0e...:
  [zakladateľ] 4a7b3c8d... (sensor_01)
       └─► 9f8e7d6c... (sensor_02)
             └─► 3b2a1f0e... (display_02)   ← tento uzol
──────────────────────────────────────────────────────
```

---

### `p2p groups`

```
p2p> p2p groups
──────────────────────────────────────────────────────
 #   group_hash        členovia  vytvoril
──────────────────────────────────────────────────────
[0]  9f8e7d6c5b4a3f2e  3         4a7b3c8d... (ja)
[1]  1a2b3c4d5e6f7a8b  2         9f8e7d6c...
──────────────────────────────────────────────────────
```

---

### `p2p vars`

```
p2p> p2p vars
──────────────────────────────────────────────────────
Vlastné premenné:
 key             typ     hodnota        prístup   updated
──────────────────────────────────────────────────────
 temperature     float   23.5           group     pred 2s
 humidity        float   61.2           group     pred 2s
 uptime          uint32  5025           public    pred 1s
 firmware_ver    string  "1.2.3"        public    pri štarte
──────────────────────────────────────────────────────
```

---

### `p2p get <id> <key>`

```
p2p> p2p get 9f8e7d6c temperature
──────────────────────────────────────────────────────
9f8e7d6c... → temperature
hodnota:  23.5 (float)
updated:  pred 1s
──────────────────────────────────────────────────────
```

---

### `p2p ping <id>`

```
p2p> p2p ping 9f8e7d6c
PING 9f8e7d6c... → 85.160.42.11:51820
  [1] 12ms  OK
  [2] 11ms  OK
  [3] 14ms  OK
avg: 12.3ms  loss: 0%
```

---

### `p2p packet log on`

Zapne výpis každého paketu (len pri `log_level=4`):

```
p2p> p2p packet log on
Packet log: ZAP

[TX] → 9f8e7d6c...  DATA_REQUEST (0x20)  msg_id:1043  97B
[RX] ← 9f8e7d6c...  DATA_RESPONSE (0x21) msg_id:1044  104B
[TX] → broadcast    HEARTBEAT (0x03)     msg_id:1045  48B
[RX] ← 4a7b3c8d...  GOSSIP (0x10)        msg_id:890   213B
```

---

### `p2p keys`

```
p2p> p2p keys
──────────────────────────────────────────────────────
POZOR: Zobrazenie kľúčov len pre debug!
──────────────────────────────────────────────────────
node_pubkey:
  4a7b3c8d 2e9f1a6b 5c0d4e8f 3a2b7c1d
  6e5f0a9b 4c3d8e2f 7a1b6c5d 0e4f3a8b

group_keys:
  [home] 9f8e7d6c: a1b2c3d4 e5f6a7b8
  [work] 1a2b3c4d: f1e2d3c4 b5a6978

node_privkey: *** SKRYTÝ (použiť p2p keys reveal) ***
──────────────────────────────────────────────────────
```

---

### `p2p db dump`

```
p2p> p2p db dump
──────────────────────────────────────────────────────
DB dump (3 uzly, 2 skupiny):

NODES:
  [0] id:4a7b... ip:85.160.42.11 ver:47 online:true
  [1] id:9f8e... ip:82.119.33.77 ver:31 online:true
  [2] id:3b2a... ip:91.200.12.44 ver:12 online:false

GROUPS:
  [0] hash:9f8e7d6c members:3 ver:8
  [1] hash:1a2b3c4d members:2 ver:3
──────────────────────────────────────────────────────
```

---

## Log levely

```c
0  →  off    žiadne výpisy
1  →  error  len chyby
2  →  warn   chyby + varovania  (default)
3  →  info   + dôležité udalosti (spojenie, skupiny)
4  →  debug  + každý paket, každý event
```

Zmena za behu:
```
p2p> p2p log 3
Log level: info
```

---

## Kompilácia bez UART (produkcia)

V `CMakeLists.txt` alebo `idf_component.yml`:

```cmake
# Vypnutie UART debug v produkcii
target_compile_definitions(micronet PRIVATE P2P_UART_DEBUG=0)
```

Pri `P2P_UART_DEBUG=0` sa všetok UART kód vycompiluje von – nulová réžia.

---

## Súbory

```
micronet/
└── src/
    └── debug/
        ├── p2p_uart.h
        ├── p2p_uart.c
        └── p2p_uart_cmds.c
```
