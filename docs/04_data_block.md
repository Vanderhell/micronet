# Blok 04 – Dáta

**Projekt:** p2plib  
**Licencia:** MIT  
**Cieľová platforma:** ESP32, Linux, Windows  
**Jazyk:** C99  
**Závislosti:** microdb, microdb_secure, microcodec, iotspool, microhealth, microbus

---

## Účel

Dátový blok je úložisko a distribučná vrstva pre dáta v sieti. Umožňuje uzlom zdieľať premenné, tabuľky a metriky. Iné uzly sa môžu pýtať na dáta (request), odoberať zmeny (subscribe) alebo čítať stav databázy. Celé je to postavené nad microdb ekosystémom.

---

## Zodpovednosti

- Ukladanie lokálnych dát (KV, tabuľky, time-series) cez microdb
- Zdieľané premenné – uzol ich zverejní, ostatní sa môžu subscribnúť
- Request/response – uzol sa opýta na dáta iného uzla
- Subscribe/notify – uzol dostane notifikáciu pri zmene hodnoty
- Query na tabuľky – štruktúrované dotazy
- Metriky a zdravie uzla cez microhealth
- Store-and-forward pre offline uzly cez iotspool
- Kompresia dát pred odoslaním cez microcodec

---

## Čo dátový blok NERIEŠI

- Transport a šifrovanie – bloky 01 a 02
- Identita uzlov a skupiny – blok 03
- Príkazy a serializácia – protokol blok

---

## Typy dát

```c
typedef enum {
    P2P_DATA_KV         = 0,   // kľúč-hodnota (napr. "temperature" = 23.5)
    P2P_DATA_TABLE      = 1,   // tabuľka so stĺpcami a riadkami
    P2P_DATA_TIMESERIES = 2,   // časová rada (ring buffer)
    P2P_DATA_VAR        = 3,   // zdieľaná premenná (s notifikáciou)
    P2P_DATA_METRIC     = 4,   // metrika uzla (RAM, uptime, spojenia...)
} p2p_data_type_t;
```

---

## Zdieľané premenné

Uzol zverejní premennú – ostatní sa subscribnú a dostanú notifikáciu pri každej zmene:

```c
// Uzol A zverejní premennú
p2p_data_publish(ctx, "temperature", P2P_TYPE_FLOAT, &temp, sizeof(float));

// Uzol B sa subscribne
p2p_data_subscribe(ctx, node_A_id, "temperature", my_callback);

// Pri zmene hodnoty na uzle A dostane B callback:
void my_callback(const char *key, const void *value, size_t len) {
    float t = *(float*)value;
}
```

---

## Request / Response

Uzol sa jednorázovo opýta na hodnotu:

```c
// Uzol B sa opýta uzla A na teplotu
p2p_data_request(ctx, node_A_id, "temperature", my_response_cb);

// Callback pri odpovedi
void my_response_cb(p2p_err_t err, const void *value, size_t len) {
    float t = *(float*)value;
}
```

---

## Query na tabuľky

```c
// Uzol B sa opýta na obsah tabuľky uzla A
p2p_data_query(ctx, node_A_id, "sensors",
               "timestamp > 1700000000",   // filter
               my_query_cb);

void my_query_cb(p2p_err_t err, const p2p_row_t *rows, uint8_t count) {
    for (int i = 0; i < count; i++) {
        // spracuj riadok
    }
}
```

---

## Metriky uzla

Každý uzol automaticky exponuje základné metriky:

```c
typedef struct {
    uint32_t uptime_s;          // čas od spustenia v sekundách
    uint32_t free_heap;         // voľná RAM v bajtoch
    uint8_t  connected_nodes;   // počet pripojených uzlov
    uint8_t  group_count;       // počet skupín
    uint32_t packets_sent;      // odoslané pakety
    uint32_t packets_recv;      // prijaté pakety
    uint32_t errors;            // chyby
    uint8_t  health_score;      // 0-100 celkové zdravie uzla
} p2p_metrics_t;
```

Ostatné uzly sa môžu opýtať:
```c
p2p_data_get_metrics(ctx, node_A_id, my_metrics_cb);
```

---

## Interné dátové štruktúry

```c
typedef struct {
    char     key[P2P_MAX_KEY_LEN];   // názov premennej
    uint8_t  type;                   // p2p_data_type_t
    uint8_t  data[P2P_MAX_VAL_LEN];  // hodnota
    size_t   data_len;
    uint32_t updated_at;             // timestamp poslednej zmeny
    bool     is_public;              // viditeľná pre iné uzly
    uint8_t  group_hash[16];         // ak 0 = všetci, inak len skupina
} p2p_var_t;

typedef struct {
    uint8_t  subscriber[32];         // node_id subscribera
    char     key[P2P_MAX_KEY_LEN];   // kľúč na ktorý je subscribnutý
    uint32_t last_notified;          // timestamp poslednej notifikácie
} p2p_subscription_t;

typedef struct {
    p2p_var_t           vars[P2P_MAX_VARS];
    uint8_t             var_count;
    p2p_subscription_t  subs[P2P_MAX_SUBS];
    uint8_t             sub_count;
    p2p_metrics_t       metrics;
    microhealth_t       health;
    iotspool_t          spool;       // pre offline uzly
} p2p_data_t;
```

---

## Konfigurácia

```c
typedef struct {
    uint8_t  max_vars;              // max počet zdieľaných premenných
    uint8_t  max_subs;             // max počet subscriptions
    uint32_t notify_min_interval_ms; // min interval medzi notifikáciami (throttle)
    bool     compress_data;         // komprimovať dáta pred odoslaním
    uint32_t spool_size;           // veľkosť offline fronty (iotspool)
} p2p_data_config_t;
```

---

## API

```c
// Inicializácia dátového bloku
p2p_err_t p2p_data_init(p2p_data_t *ctx, const p2p_data_config_t *cfg);

// Zverejnenie premennej (lokálne + dostupné pre iných)
p2p_err_t p2p_data_publish(p2p_data_t *ctx, const char *key,
                            p2p_data_type_t type,
                            const void *value, size_t len);

// Aktualizácia hodnoty (spustí notifikáciu subscriberom)
p2p_err_t p2p_data_update(p2p_data_t *ctx, const char *key,
                           const void *value, size_t len);

// Jednorázový request na hodnotu iného uzla
p2p_err_t p2p_data_request(p2p_data_t *ctx, const uint8_t node_id[32],
                            const char *key,
                            void (*cb)(p2p_err_t, const void*, size_t));

// Subscribe na zmeny premennej iného uzla
p2p_err_t p2p_data_subscribe(p2p_data_t *ctx, const uint8_t node_id[32],
                              const char *key,
                              void (*cb)(const char*, const void*, size_t));

// Zrušenie subscribe
p2p_err_t p2p_data_unsubscribe(p2p_data_t *ctx, const uint8_t node_id[32],
                                const char *key);

// Query na tabuľku iného uzla
p2p_err_t p2p_data_query(p2p_data_t *ctx, const uint8_t node_id[32],
                          const char *table, const char *filter,
                          void (*cb)(p2p_err_t, const p2p_row_t*, uint8_t));

// Získanie metrík uzla
p2p_err_t p2p_data_get_metrics(p2p_data_t *ctx, const uint8_t node_id[32],
                                void (*cb)(p2p_err_t, const p2p_metrics_t*));

// Zoznam dostupných premenných uzla
p2p_err_t p2p_data_list_vars(p2p_data_t *ctx, const uint8_t node_id[32],
                              void (*cb)(p2p_err_t, const char**, uint8_t));

// Tick – spracovanie offline fronty, throttle notifikácií
p2p_err_t p2p_data_tick(p2p_data_t *ctx);

// Uvoľnenie zdrojov
void p2p_data_deinit(p2p_data_t *ctx);
```

---

## Eventy (microbus)

```c
P2P_EVENT_VAR_UPDATED       // lokálna premenná aktualizovaná
P2P_EVENT_VAR_NOTIFY        // notifikácia od subscribnutého uzla
P2P_EVENT_REQUEST_IN        // iný uzol sa pýta na dáta
P2P_EVENT_QUERY_IN          // iný uzol posiela query
P2P_EVENT_SPOOL_FLUSH       // offline fronta sa vyprázdňuje
```

---

## Oprávnenia

Každá premenná môže mať obmedzenie kto ju môže čítať:

```c
typedef enum {
    P2P_ACCESS_PUBLIC   = 0,   // čítať môže ktokoľvek v sieti
    P2P_ACCESS_GROUP    = 1,   // len členovia danej skupiny
    P2P_ACCESS_PRIVATE  = 2,   // len explicitne povolené uzly
} p2p_access_t;
```

---

## Stavový automat (microfsm)

```
IDLE
 └─► READY
       ├─► RESPONDING    (spracovanie request/query)
       ├─► NOTIFYING     (odosielanie notifikácií)
       └─► SPOOLING      (ukladanie pre offline uzol)
             └─► FLUSHING  (vyprázdňovanie keď uzol online)
```

---

## Chybové kódy

```c
typedef enum {
    P2P_DATA_OK             =  0,
    P2P_DATA_ERR_NOT_FOUND  = -1,  // premenná neexistuje
    P2P_DATA_ERR_ACCESS     = -2,  // nemáš prístup
    P2P_DATA_ERR_TYPE       = -3,  // nesprávny typ dát
    P2P_DATA_ERR_FULL       = -4,  // max_vars dosiahnuté
    P2P_DATA_ERR_TIMEOUT    = -5,  // request timeout
    P2P_DATA_ERR_OFFLINE    = -6,  // uzol offline (spoolované)
} p2p_data_err_t;
```

---

## Testovací plán (microtest)

### Test 01 – Publish a request
- Uzol A zverejní premennú "temperature" = 23.5
- Uzol B zavolá `p2p_data_request` na node_A / "temperature"
- **Očakávaný výsledok:** B dostane 23.5

### Test 02 – Subscribe a notify
- B sa subscribne na "temperature" uzla A
- A aktualizuje hodnotu na 25.0
- **Očakávaný výsledok:** B dostane callback s 25.0

### Test 03 – Throttle notifikácií
- B subscribnutý, A aktualizuje 100x za sekundu
- `notify_min_interval_ms = 1000`
- **Očakávaný výsledok:** B dostane max 1 notifikáciu za sekundu

### Test 04 – Query tabuľky
- A má tabuľku "sensors" s 10 riadkami
- B zavolá query s filtrom
- **Očakávaný výsledok:** B dostane len filtrované riadky

### Test 05 – Metriky
- B sa opýta na metriky uzla A
- **Očakávaný výsledok:** platná `p2p_metrics_t` štruktúra

### Test 06 – Offline spool
- A pošle notifikáciu B ktorý je offline
- B sa pripojí
- **Očakávaný výsledok:** B dostane notifikáciu po pripojení

### Test 07 – Oprávnenia
- A zverejní premennú s `P2P_ACCESS_GROUP`
- C (nie je v skupine) sa opýta
- **Očakávaný výsledok:** `P2P_DATA_ERR_ACCESS`

### Test 08 – Zoznam premenných
- A má 3 zverejnené premenné
- B zavolá `p2p_data_list_vars`
- **Očakávaný výsledok:** B dostane zoznam 3 kľúčov

---

## Súbory

```
p2plib/
└── src/
    └── data/
        ├── p2p_data.h
        ├── p2p_data.c
        ├── p2p_data_vars.c
        ├── p2p_data_query.c
        ├── p2p_data_metrics.c
        └── tests/
            └── test_data.c
```

---

## Poznámky

- Na ESP32 odporúčame `max_vars = 16` a `max_subs = 8` kvôli RAM
- Kompresia cez microcodec sa aktivuje automaticky ak dáta > 64 B
- iotspool zachová správy aj po reštarte ESP32 (uložené vo flash)
- Metriky sú vždy public – neaplikujú sa access obmedzenia
- `p2p_data_tick()` volaj z rovnakého tasku ako ostatné tick funkcie
