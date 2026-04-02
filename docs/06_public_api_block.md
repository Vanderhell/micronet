# Blok 06 – Public API

**Projekt:** p2plib  
**Licencia:** MIT  
**Cieľová platforma:** ESP32, Linux, Windows  
**Jazyk:** C99  
**Závislosti:** všetky bloky 01–05

---

## Účel

Public API je jediné miesto cez ktoré developer komunikuje s knižnicou. Skrýva komplexitu všetkých nižších blokov za jednoduché funkcie. Developer nastaví konfig, zavolá `p2p_init()`, a potom už len posiela príkazy. Nič iné nemusí riešiť.

---

## Filozofia

- **Jeden include** – `#include "p2plib.h"` a máš všetko
- **Jeden init** – `p2p_init(&cfg)` inicializuje všetky bloky
- **Jeden tick** – `p2p_tick()` v hlavnej slučke, zvyšok je automatické
- **Jeden config** – všetky nastavenia na jednom mieste
- **Zero surprise** – každá funkcia vráti jasný chybový kód

---

## Hlavná konfigurácia

```c
typedef struct {

    // === Identita uzla ===
    uint8_t  node_privkey[32];      // súkromný kľúč (0 = generuj automaticky)
    const char *node_name;          // ľudsky čitateľný názov uzla (voliteľné)

    // === Sieť ===
    const char *stun_host;          // STUN server (default: "stun.l.google.com")
    uint16_t    stun_port;          // STUN port (default: 19302)
    uint16_t    local_port;         // lokálny UDP port (0 = automaticky)

    // === Skupiny ===
    struct {
        uint8_t  group_hash[16];    // hash skupiny
        uint8_t  group_key[16];     // kľúč skupiny
    } groups[P2P_MAX_GROUPS];
    uint8_t group_count;

    // === Timeouty ===
    uint32_t heartbeat_ms;          // interval heartbeat (default: 5000)
    uint32_t offline_timeout_ms;    // kedy je uzol offline (default: 15000)
    uint32_t retry_interval_ms;     // retry interval (default: 2000)
    uint8_t  retry_count;           // počet pokusov (default: 3)

    // === Pamäť ===
    uint8_t  max_nodes;             // max uzlov v DB (default: 16)
    uint8_t  max_vars;              // max zdieľaných premenných (default: 16)
    uint8_t  max_pending;           // max čakajúcich správ (default: 8)

    // === Logovanie ===
    uint8_t  log_level;             // 0=off 1=err 2=warn 3=info 4=debug

    // === Rozšírenia ===
    void (*on_node_online)(const uint8_t node_id[32]);
    void (*on_node_offline)(const uint8_t node_id[32]);
    void (*on_custom_msg)(const p2p_message_t *msg);

} p2p_config_t;
```

---

## Minimálna inicializácia (ESP32 príklad)

```c
#include "p2plib.h"

static p2p_config_t cfg = {
    .stun_host        = "stun.l.google.com",
    .stun_port        = 19302,
    .heartbeat_ms     = 5000,
    .offline_timeout_ms = 15000,
    .log_level        = 2,
    .on_node_online   = on_online,
    .on_node_offline  = on_offline,
};

void app_main(void) {
    p2p_init(&cfg);

    while (1) {
        p2p_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

## Kompletné Public API

### Životný cyklus

```c
// Inicializácia celej knižnice
p2p_err_t p2p_init(const p2p_config_t *cfg);

// Tick – volaj periodicky (každých 10ms)
p2p_err_t p2p_tick(void);

// Získanie vlastného node_id (verejný kľúč)
p2p_err_t p2p_get_node_id(uint8_t out_node_id[32]);

// Uvoľnenie všetkých zdrojov
void p2p_deinit(void);
```

---

### Uzly

```c
// Je daný uzol online?
bool p2p_node_is_online(const uint8_t node_id[32]);

// Zoznam online uzlov
p2p_err_t p2p_node_list_online(uint8_t out[][32], uint8_t *count);

// Zoznam všetkých známych uzlov
p2p_err_t p2p_node_list_all(uint8_t out[][32], uint8_t *count);

// Kto pridal daný uzol (web of trust)
p2p_err_t p2p_node_invited_by(const uint8_t node_id[32],
                               uint8_t out_inviter[32]);
```

---

### Skupiny

```c
// Vytvorenie novej skupiny
p2p_err_t p2p_group_create(uint8_t out_group_hash[16],
                            uint8_t out_group_key[16]);

// Pozvanie uzla do skupiny
p2p_err_t p2p_group_invite(const uint8_t node_id[32],
                            const uint8_t group_hash[16]);

// Prijatie pozvánky
p2p_err_t p2p_group_join(const uint8_t group_hash[16],
                          const uint8_t group_key[16]);

// Odchod zo skupiny
p2p_err_t p2p_group_leave(const uint8_t group_hash[16]);

// Zoznam členov skupiny
p2p_err_t p2p_group_members(const uint8_t group_hash[16],
                              uint8_t out[][32], uint8_t *count);

// Je uzol členom skupiny?
bool p2p_group_is_member(const uint8_t node_id[32],
                          const uint8_t group_hash[16]);
```

---

### Dáta – zdieľané premenné

```c
// Zverejnenie premennej
p2p_err_t p2p_publish(const char *key, const void *value, size_t len);

// Aktualizácia hodnoty (notifikuje subscriberov)
p2p_err_t p2p_update(const char *key, const void *value, size_t len);

// Jednorázový request na hodnotu iného uzla
p2p_err_t p2p_request(const uint8_t node_id[32], const char *key,
                       void (*cb)(p2p_err_t, const void*, size_t));

// Subscribe na zmeny premennej
p2p_err_t p2p_subscribe(const uint8_t node_id[32], const char *key,
                         void (*cb)(const char*, const void*, size_t));

// Zrušenie subscribe
p2p_err_t p2p_unsubscribe(const uint8_t node_id[32], const char *key);

// Zoznam premenných uzla
p2p_err_t p2p_list_vars(const uint8_t node_id[32],
                         void (*cb)(p2p_err_t, const char**, uint8_t));
```

---

### Dáta – tabuľky a metriky

```c
// Query na tabuľku iného uzla
p2p_err_t p2p_query(const uint8_t node_id[32],
                     const char *table, const char *filter,
                     void (*cb)(p2p_err_t, const p2p_row_t*, uint8_t));

// Metriky uzla
p2p_err_t p2p_get_metrics(const uint8_t node_id[32],
                           void (*cb)(p2p_err_t, const p2p_metrics_t*));
```

---

### Vlastné správy

```c
// Odoslanie vlastnej správy uzlu
p2p_err_t p2p_send_custom(const uint8_t node_id[32],
                           uint8_t msg_type,
                           const uint8_t *payload, size_t len);

// Broadcast vlastnej správy do skupiny
p2p_err_t p2p_broadcast_custom(const uint8_t group_hash[16],
                                uint8_t msg_type,
                                const uint8_t *payload, size_t len);

// Registrácia handlera pre vlastný typ správy
p2p_err_t p2p_register_handler(uint8_t msg_type,
                                void (*handler)(const uint8_t src[32],
                                                const uint8_t *payload,
                                                size_t len));
```

---

## Chybové kódy (zjednotené)

```c
typedef enum {
    P2P_OK                  =  0,
    P2P_ERR_NOT_INIT        = -1,   // p2p_init() nebol zavolaný
    P2P_ERR_INVALID_ARG     = -2,   // neplatný argument
    P2P_ERR_NOT_FOUND       = -3,   // uzol / premenná nenájdená
    P2P_ERR_ACCESS          = -4,   // nemáš prístup
    P2P_ERR_OFFLINE         = -5,   // uzol offline
    P2P_ERR_TIMEOUT         = -6,   // timeout
    P2P_ERR_FULL            = -7,   // buffer / DB plná
    P2P_ERR_CRYPTO          = -8,   // chyba šifrovania
    P2P_ERR_TRANSPORT       = -9,   // chyba siete
    P2P_ERR_INTERNAL        = -10,  // interná chyba
} p2p_err_t;
```

---

## Príklady použitia

### ESP32 – senzor teploty

```c
// Uzol A – senzor
void sensor_task(void *arg) {
    float temp = read_temperature();
    p2p_publish("temperature", &temp, sizeof(float));

    while (1) {
        temp = read_temperature();
        p2p_update("temperature", &temp, sizeof(float));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Uzol B – display
void on_temp(const char *key, const void *val, size_t len) {
    float t = *(float*)val;
    display_show_temperature(t);
}

void display_task(void *arg) {
    p2p_subscribe(sensor_node_id, "temperature", on_temp);
    while (1) {
        p2p_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

### Vlastný príkaz – poplach

```c
#define CMD_ALARM  0x80

// Odosielateľ
p2p_send_custom(node_id, CMD_ALARM, "fire", 4);

// Príjemca
void on_alarm(const uint8_t src[32], const uint8_t *payload, size_t len) {
    trigger_alarm();
}
p2p_register_handler(CMD_ALARM, on_alarm);
```

---

## Testovací plán (microtest)

### Test 01 – Init a deinit
- Inicializuj s minimálnym konfig
- Over že `p2p_get_node_id()` vráti platný ID
- Deinicializuj
- **Očakávaný výsledok:** `P2P_OK`, čistý lifecycle

### Test 02 – Publish a request (end-to-end)
- Uzol A: `p2p_publish("val", &x, sizeof(x))`
- Uzol B: `p2p_request(node_A, "val", cb)`
- **Očakávaný výsledok:** cb zavolaný so správnou hodnotou

### Test 03 – Subscribe end-to-end
- B subscribnutý na "val" uzla A
- A zavolá `p2p_update()`
- **Očakávaný výsledok:** B dostane callback

### Test 04 – Skupina end-to-end
- A vytvorí skupinu, pozve B
- B prijme pozvánku
- Over `p2p_group_is_member(B, group_hash) == true`
- **Očakávaný výsledok:** B je členom

### Test 05 – Vlastná správa end-to-end
- Registruj handler pre `0x80`
- A pošle custom správu B
- **Očakávaný výsledok:** handler zavolaný na strane B

### Test 06 – Node online/offline callback
- Pripoj uzol A
- Over že `on_node_online` zavolaný
- Odpoj uzol A
- **Očakávaný výsledok:** `on_node_offline` zavolaný

---

## Štruktúra celého projektu

```
p2plib/
├── include/
│   └── p2plib.h              ← jediný include pre developera
├── src/
│   ├── transport/            ← Blok 01
│   ├── security/             ← Blok 02
│   ├── network/              ← Blok 03
│   ├── data/                 ← Blok 04
│   ├── protocol/             ← Blok 05
│   └── p2plib.c              ← Blok 06 – Public API implementácia
├── hal/
│   ├── p2p_hal_linux.c
│   └── p2p_hal_esp32.c
├── tests/
│   ├── test_transport.c
│   ├── test_security.c
│   ├── test_network.c
│   ├── test_data.c
│   ├── test_protocol.c
│   └── test_api.c            ← end-to-end testy
├── examples/
│   ├── esp32_sensor/
│   └── linux_chat/
├── CMakeLists.txt
├── idf_component.yml         ← ESP-IDF komponent
├── LICENSE                   ← MIT
└── README.md
```

---

## Poznámky

- Na ESP32 odporúčame volať `p2p_tick()` z dedikovaného FreeRTOS tasku s prioritou 5
- Všetky callbacky sú volané z kontextu `p2p_tick()` – nie sú thread-safe
- Ak potrebuješ thread-safety, obaľ volania mutexom na úrovni aplikácie
- `p2p_init()` alokuje všetku pamäť jednorazovo pri štarte – žiadne ďalšie malloc
- Knižnica je plne kompatibilná s ESP-IDF ako komponent (`idf_component.yml`)
