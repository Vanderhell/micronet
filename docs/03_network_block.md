# Blok 03 – Sieť (uzly, skupiny, gossip)

**Projekt:** micronet
**Licencia:** MIT
**Cieľová platforma:** ESP32, Linux, Windows
**Jazyk:** C99
**Závislosti:** microdb, microdb_secure, microbus, microtimer, microfsm

---

## Účel

Sieťový blok spravuje identitu uzlov, členstvo v skupinách, šírenie informácií o uzloch (gossip protokol) a reťaz dôvery (web of trust). Je to "adresár" celej siete – vie kto existuje, do akej skupiny patrí a kto koho pridal.

---

## Zodpovednosti

- Správa lokálnej databázy uzlov (microdb)
- Správa skupín – hash, kľúč, členovia
- Gossip protokol – šírenie informácií o nových uzloch
- Web of trust – reťaz kto koho pridal
- Synchronizácia databázy medzi uzlami
- Online/offline stav uzlov
- Pozvánky do skupín

---

## Čo sieťový blok NERIEŠI

- Fyzický transport – to je transport blok
- Šifrovanie – to je bezpečnostný blok
- Obsah správ a príkazov – to je protokol blok

---

## Dátové štruktúry

```c
// Uzol v sieti
typedef struct {
    uint8_t  node_id[32];          // verejný kľúč = identita uzla
    uint8_t  external_ip[4];       // posledná známa vonkajšia IP
    uint16_t external_port;        // posledný známy port
    uint8_t  invited_by[32];       // node_id toho kto ho pridal (0 = zakladateľ)
    uint32_t first_seen;           // timestamp prvého stretnutia
    uint32_t last_seen;            // timestamp posledného heartbeat
    uint8_t  group_hashes[P2P_MAX_GROUPS][16]; // skupiny do ktorých patrí
    uint8_t  group_count;          // počet skupín
    bool     is_online;            // aktuálny stav
    uint32_t db_version;           // verzia záznamu (pre sync)
} p2p_node_t;

// Skupina
typedef struct {
    uint8_t  group_hash[16];       // identifikátor skupiny (verejný)
    uint8_t  group_key[16];        // šifrovací kľúč (tajný, len členovia)
    uint8_t  created_by[32];       // node_id zakladateľa
    uint32_t created_at;           // timestamp vytvorenia
    uint8_t  members[P2P_MAX_MEMBERS][32]; // node_id členov
    uint8_t  member_count;         // počet členov
    uint32_t db_version;           // verzia záznamu (pre sync)
} p2p_group_t;

// Hlavný kontext sieťového bloku
typedef struct {
    p2p_node_t   self;             // vlastný uzol
    p2p_node_t   nodes[P2P_MAX_NODES];   // známe uzly
    uint8_t      node_count;
    p2p_group_t  groups[P2P_MAX_GROUPS]; // skupiny
    uint8_t      group_count;
    microfsm_t   fsm;
    microtimer_t gossip_timer;     // periodické šírenie informácií
    microtimer_t sync_timer;       // periodická synchronizácia DB
} p2p_network_t;
```

---

## Konfigurácia

```c
typedef struct {
    uint32_t gossip_interval_ms;   // ako často šíriť gossip (napr. 30000)
    uint32_t sync_interval_ms;     // ako často synchronizovať DB (napr. 60000)
    uint32_t offline_timeout_ms;   // po koľkých ms je uzol offline (napr. 20000)
    uint8_t  max_nodes;            // maximálny počet uzlov
    uint8_t  max_groups;           // maximálny počet skupín
} p2p_network_config_t;
```

---

## Gossip protokol

Každý uzol periodicky odosiela všetkým známym uzlom správu o sebe a o nových uzloch ktoré objavil.

```
Uzol A objaví nový uzol G:
  1. Pridá G do svojej lokálnej DB
  2. Odošle GOSSIP správu všetkým B, C, D, E, F:
     { type: GOSSIP_NEW_NODE, node: G, invited_by: A, version: X }
  3. B, C, D, E, F si pridajú G do svojej DB
  4. G dostane GOSSIP správu o všetkých ostatných uzloch
```

Gossip správa obsahuje len rozdielové záznamy (delta) – nie celú DB.

---

## Pozvánka do skupiny

```
Uzol A pozýva uzol B do skupiny:
  1. A zavolá p2p_group_invite(node_B_id, group_hash)
  2. Knižnica zašifruje group_key verejným kľúčom B
  3. Odošle INVITE správu:
     { type: GROUP_INVITE, group_hash, encrypted_group_key, invited_by: A }
  4. B dešifruje group_key svojim súkromným kľúčom
  5. B zavolá p2p_group_join(group_hash) – prijme pozvánku
  6. Gossip oznámi ostatným členom skupiny že B je nový člen
```

---

## Synchronizácia databázy

Každý záznam má `db_version` (monotónne rastúce číslo + podpis autora).

Pri stretnutí dvoch uzlov:
1. Vymenia si zoznam verzií
2. Požiadajú o záznamy ktoré nemajú
3. Novšia verzia vždy vyhrá (last-write-wins)
4. Podpis overí bezpečnostný blok – nikto nemôže falšovať cudzí záznam

---

## API

```c
// Inicializácia sieťového bloku
p2p_err_t p2p_network_init(p2p_network_t *ctx, const p2p_network_config_t *cfg,
                            const uint8_t self_node_id[32]);

// Pridanie uzla do lokálnej DB (pri prvom stretnutí)
p2p_err_t p2p_network_add_node(p2p_network_t *ctx, const p2p_node_t *node);

// Vyhľadanie uzla podľa node_id
p2p_err_t p2p_network_find_node(p2p_network_t *ctx, const uint8_t node_id[32],
                                 p2p_node_t *out);

// Aktualizácia online stavu uzla
p2p_err_t p2p_network_set_online(p2p_network_t *ctx, const uint8_t node_id[32], bool online);

// Vytvorenie novej skupiny
p2p_err_t p2p_network_group_create(p2p_network_t *ctx, uint8_t out_group_hash[16]);

// Pozvanie uzla do skupiny
p2p_err_t p2p_network_group_invite(p2p_network_t *ctx, const uint8_t node_id[32],
                                    const uint8_t group_hash[16]);

// Prijatie pozvánky do skupiny
p2p_err_t p2p_network_group_join(p2p_network_t *ctx, const uint8_t group_hash[16],
                                  const uint8_t group_key[16]);

// Odchod zo skupiny
p2p_err_t p2p_network_group_leave(p2p_network_t *ctx, const uint8_t group_hash[16]);

// Zoznam členov skupiny
p2p_err_t p2p_network_group_members(p2p_network_t *ctx, const uint8_t group_hash[16],
                                     uint8_t out_members[][32], uint8_t *count);

// Tick – gossip, sync, online/offline detekcia
p2p_err_t p2p_network_tick(p2p_network_t *ctx);

// Spracovanie prijatej gossip správy
p2p_err_t p2p_network_on_gossip(p2p_network_t *ctx, const uint8_t *msg, size_t len);

// Uvoľnenie zdrojov
void p2p_network_deinit(p2p_network_t *ctx);
```

---

## Eventy (microbus)

Sieťový blok publikuje eventy ktoré môžu odoberať vyššie bloky:

```c
P2P_EVENT_NODE_ONLINE      // uzol sa pripojil
P2P_EVENT_NODE_OFFLINE     // uzol vypadol
P2P_EVENT_NODE_NEW         // nový uzol objavený cez gossip
P2P_EVENT_GROUP_INVITE     // pozvánka do skupiny
P2P_EVENT_GROUP_JOINED     // člen sa pridal do skupiny
P2P_EVENT_GROUP_LEFT       // člen odišiel zo skupiny
P2P_EVENT_DB_SYNCED        // synchronizácia DB dokončená
```

---

## Stavový automat (microfsm)

```
IDLE
 └─► JOINING          (prvé pripojenie, výmena uzlov)
       └─► ACTIVE
             ├─► GOSSIPING     (periodické šírenie)
             ├─► SYNCING       (synchronizácia DB)
             └─► ISOLATED      (žiadny uzol nedostupný)
                   └─► ACTIVE  (keď sa uzol znovu nájde)
```

---

## Chybové kódy

```c
typedef enum {
    P2P_NET_OK               =  0,
    P2P_NET_ERR_NODE_EXISTS  = -1,  // uzol už existuje v DB
    P2P_NET_ERR_NODE_FULL    = -2,  // DB plná, max_nodes dosiahnuté
    P2P_NET_ERR_NOT_FOUND    = -3,  // uzol nenájdený
    P2P_NET_ERR_NOT_MEMBER   = -4,  // nie si členom skupiny
    P2P_NET_ERR_GROUP_FULL   = -5,  // skupina plná
    P2P_NET_ERR_NO_INVITE    = -6,  // nemáš právo pozvať
    P2P_NET_ERR_SYNC         = -7,  // synchronizácia zlyhala
} p2p_net_err_t;
```

---

## Testovací plán (microtest)

### Test 01 – Pridanie uzla
- Vytvor sieťový kontext
- Pridaj uzol s platným node_id
- Vyhľadaj ho podľa node_id
- **Očakávaný výsledok:** `P2P_NET_OK`, záznam nájdený

### Test 02 – Online / offline detekcia
- Pridaj uzol, nastav `is_online = true`
- Počkaj dlhšie ako `offline_timeout_ms` bez heartbeat
- Zavolaj tick
- **Očakávaný výsledok:** `P2P_EVENT_NODE_OFFLINE` publikovaný

### Test 03 – Vytvorenie skupiny
- Zavolaj `p2p_network_group_create()`
- Over že `group_hash` nie je samé nuly
- **Očakávaný výsledok:** `P2P_NET_OK`, platný hash

### Test 04 – Pozvánka a prijatie
- Uzol A vytvorí skupinu
- A pozve B (`p2p_network_group_invite`)
- B prijme (`p2p_network_group_join`)
- Over že B je v zozname členov
- **Očakávaný výsledok:** `P2P_NET_OK`, B v skupne

### Test 05 – Gossip šírenie
- Uzol A a B sú spojení
- A objaví nový uzol C
- A spustí gossip
- Over že B má C vo svojej DB
- **Očakávaný výsledok:** C v DB uzla B

### Test 06 – Synchronizácia DB
- A a B majú rôzne verzie záznamov
- Spusti sync
- Over že obaja majú zhodné záznamy
- **Očakávaný výsledok:** DB zhodná na oboch stranách

### Test 07 – Web of trust
- A pridal B, B pridal C
- Over že `invited_by` reťaz je A→B→C
- **Očakávaný výsledok:** reťaz dôvery správna

### Test 08 – Odchod zo skupiny
- B je členom skupiny
- B zavolá `p2p_network_group_leave()`
- Over že B nie je v zozname členov
- Over že ostatní dostali `P2P_EVENT_GROUP_LEFT`
- **Očakávaný výsledok:** B odstránený, event publikovaný

---

## Súbory

```
micronet/
└── src/
    └── network/
        ├── p2p_network.h
        ├── p2p_network.c
        ├── p2p_network_gossip.c
        ├── p2p_network_group.c
        ├── p2p_network_sync.c
        └── tests/
            └── test_network.c
```

---

## Poznámky

- Gossip delta obsahuje len záznamy novšie ako posledná synchronizácia – šetrí bandwidth
- Na ESP32 je `P2P_MAX_NODES` obmedzené dostupnou RAM – odporúčame max 32 uzlov
- group_hash je `HMAC-SHA256(group_key + created_at)[0..15]` – deterministický, odvodený z kľúča
- Člen môže pozvať iného člena len do skupiny kde sám je členom
- DB verzia je `uint32_t` – pri pretečení sa resetuje a spustí plná synchronizácia
