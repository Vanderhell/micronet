# Blok 05 – Protokol

**Projekt:** micronet
**Licencia:** MIT
**Cieľová platforma:** ESP32, Linux, Windows
**Jazyk:** C99
**Závislosti:** microcbor, microbus, microfsm, microlog, microres

---

## Účel

Protokol blok je mozog knižnice. Definuje formát všetkých správ, serializuje a deserializuje príkazy cez microcbor, riadi životný cyklus spojenia cez microfsm a prepája všetky nižšie bloky cez microbus. Developer komunikuje výhradne cez protokol blok – nikdy priamo s nižšími vrstvami.

---

## Zodpovednosti

- Definícia a serializácia všetkých typov správ (microcbor)
- Dispatch prichádzajúcich správ na správny handler
- Životný cyklus spojenia (microfsm)
- Logovanie všetkej komunikácie (microlog)
- Retry nedoručených správ (microres)
- Prepojenie transport → security → network → data blokov cez microbus
- Rozšíriteľnosť – vlastné typy správ od developera

---

## Čo protokol blok NERIEŠI

- Fyzický transport – blok 01
- Šifrovanie – blok 02
- Správa uzlov a skupín – blok 03
- Ukladanie dát – blok 04

---

## Typy správ

```c
typedef enum {
    // Spojenie
    P2P_MSG_HELLO           = 0x01,  // handshake – ahoj, tu je môj pubkey
    P2P_MSG_HELLO_ACK       = 0x02,  // potvrdenie handshake
    P2P_MSG_HEARTBEAT       = 0x03,  // som živý
    P2P_MSG_DISCONNECT      = 0x04,  // odchádzam

    // Sieť
    P2P_MSG_GOSSIP          = 0x10,  // informácie o uzloch
    P2P_MSG_SYNC_REQ        = 0x11,  // žiadosť o synchronizáciu DB
    P2P_MSG_SYNC_DATA       = 0x12,  // dáta synchronizácie
    P2P_MSG_GROUP_INVITE    = 0x13,  // pozvánka do skupiny
    P2P_MSG_GROUP_JOIN      = 0x14,  // prijatie pozvánky
    P2P_MSG_GROUP_LEAVE     = 0x15,  // odchod zo skupiny

    // Dáta
    P2P_MSG_DATA_REQUEST    = 0x20,  // žiadosť o hodnotu premennej
    P2P_MSG_DATA_RESPONSE   = 0x21,  // odpoveď s hodnotou
    P2P_MSG_DATA_NOTIFY     = 0x22,  // notifikácia zmeny
    P2P_MSG_DATA_SUBSCRIBE  = 0x23,  // subscribe na premennú
    P2P_MSG_DATA_UNSUBSCRIBE= 0x24,  // zrušenie subscribe
    P2P_MSG_QUERY_REQ       = 0x25,  // query na tabuľku
    P2P_MSG_QUERY_RESP      = 0x26,  // výsledok query
    P2P_MSG_METRICS_REQ     = 0x27,  // žiadosť o metriky
    P2P_MSG_METRICS_RESP    = 0x28,  // odpoveď s metrikami

    // Rozšírenia (developer)
    P2P_MSG_CUSTOM          = 0x80,  // vlastné správy od 0x80 vyššie
} p2p_msg_type_t;
```

---

## Formát správy (CBOR)

Každá správa je CBOR mapa:

```
{
  "t": <uint8>        // typ správy (p2p_msg_type_t)
  "id": <uint16>      // message ID (pre ACK / retry)
  "ts": <uint32>      // timestamp odoslania
  "src": <bytes32>    // node_id odosielateľa
  "dst": <bytes32>    // node_id príjemcu (0 = broadcast)
  "grp": <bytes16>    // group_hash (0 = žiadna skupina)
  "pay": <bytes>      // payload (závisí od typu správy)
}
```

Payload každého typu je tiež CBOR – príklad pre `DATA_REQUEST`:

```
{
  "key": "temperature"
  "rid": <uint16>     // request ID pre spárovanie odpovede
}
```

---

## Interné dátové štruktúry

```c
typedef struct {
    uint8_t  type;               // p2p_msg_type_t
    uint16_t msg_id;             // unikátne ID správy
    uint32_t timestamp;          // čas odoslania
    uint8_t  src[32];            // odosielateľ
    uint8_t  dst[32];            // príjemca (0 = broadcast)
    uint8_t  group_hash[16];     // skupina (0 = žiadna)
    uint8_t  payload[P2P_MAX_PAYLOAD];
    size_t   payload_len;
} p2p_message_t;

typedef struct {
    uint16_t msg_id;             // čakajúce msg_id
    uint32_t sent_at;            // kedy odoslané
    uint8_t  retry_count;        // koľkokrát skúšané
    p2p_message_t msg;           // kópia správy pre retry
} p2p_pending_t;

typedef struct {
    uint16_t         next_msg_id;
    p2p_pending_t    pending[P2P_MAX_PENDING];
    uint8_t          pending_count;
    microfsm_t       fsm;
    microlog_t       log;
    microres_t       retry;
    // callbacks na nižšie bloky
    p2p_transport_t  *transport;
    p2p_security_t   *security;
    p2p_network_t    *network;
    p2p_data_t       *data;
    // custom handler
    void (*custom_handler)(const p2p_message_t *msg);
} p2p_protocol_t;
```

---

## Konfigurácia

```c
typedef struct {
    uint8_t  max_pending;           // max čakajúcich správ na ACK
    uint32_t retry_interval_ms;     // interval retry
    uint8_t  retry_count;           // počet pokusov
    uint8_t  log_level;             // microlog úroveň
    void (*custom_handler)(const p2p_message_t *msg); // vlastné správy
} p2p_protocol_config_t;
```

---

## API

```c
// Inicializácia protokol bloku (prepojí všetky bloky)
p2p_err_t p2p_protocol_init(p2p_protocol_t *ctx,
                             const p2p_protocol_config_t *cfg,
                             p2p_transport_t *transport,
                             p2p_security_t  *security,
                             p2p_network_t   *network,
                             p2p_data_t      *data);

// Odoslanie správy
p2p_err_t p2p_protocol_send(p2p_protocol_t *ctx, const p2p_message_t *msg);

// Broadcast správy do skupiny
p2p_err_t p2p_protocol_broadcast(p2p_protocol_t *ctx,
                                  const uint8_t group_hash[16],
                                  const p2p_message_t *msg);

// Spracovanie prijatého raw paketu (volá transport blok)
p2p_err_t p2p_protocol_on_packet(p2p_protocol_t *ctx,
                                  const uint8_t *data, size_t len,
                                  const uint8_t src_ip[4], uint16_t src_port);

// Registrácia vlastného handlera pre custom správy
p2p_err_t p2p_protocol_register_handler(p2p_protocol_t *ctx,
                                         uint8_t msg_type,
                                         void (*handler)(const p2p_message_t*));

// Tick – retry, heartbeat, logovanie
p2p_err_t p2p_protocol_tick(p2p_protocol_t *ctx);

// Uvoľnenie zdrojov
void p2p_protocol_deinit(p2p_protocol_t *ctx);
```

---

## Tok správy cez bloky

```
ODOSLANIE:
  Developer → p2p_protocol_send()
    → microcbor serializuj
    → p2p_security encrypt()
    → p2p_transport send()
    → [sieť]

PRÍJEM:
  [sieť] → p2p_transport recv()
    → p2p_security decrypt()
    → microcbor deserializuj
    → p2p_protocol dispatch()
      ├─► network handler  (GOSSIP, GROUP_*)
      ├─► data handler     (DATA_*, QUERY_*, METRICS_*)
      ├─► session handler  (HELLO, HEARTBEAT, DISCONNECT)
      └─► custom handler   (MSG_CUSTOM)
```

---

## Životný cyklus spojenia (microfsm)

```
BOOT
 └─► INIT           (inicializácia všetkých blokov)
       └─► STUN     (zistenie vonkajšej IP)
             └─► READY
                   ├─► CONNECTING    (handshake s uzlom)
                   │     └─► ACTIVE  (plné spojenie)
                   │           └─► DISCONNECTING → READY
                   └─► ISOLATED      (žiadny uzol)
                         └─► READY   (keď sa uzol nájde)
```

---

## Rozšíriteľnosť – vlastné správy

Developer môže pridať vlastné typy správ (0x80 – 0xFF):

```c
// Definícia vlastnej správy
#define MY_MSG_ALARM  0x80

// Registrácia handlera
p2p_protocol_register_handler(ctx, MY_MSG_ALARM, on_alarm);

// Odoslanie vlastnej správy
p2p_message_t msg = {
    .type = MY_MSG_ALARM,
    .payload = ...,
};
p2p_protocol_send(ctx, &msg);

// Handler na strane príjemcu
void on_alarm(const p2p_message_t *msg) {
    // spracuj poplach
}
```

---

## Eventy (microbus)

```c
P2P_EVENT_MSG_SENT          // správa odoslaná
P2P_EVENT_MSG_RECV          // správa prijatá
P2P_EVENT_MSG_ACK           // potvrdenie doručenia
P2P_EVENT_MSG_RETRY         // opakovanie odoslania
P2P_EVENT_MSG_FAILED        // správa nedoručená po všetkých pokusoch
P2P_EVENT_SESSION_UP        // spojenie nadviazané
P2P_EVENT_SESSION_DOWN      // spojenie ukončené
```

---

## Chybové kódy

```c
typedef enum {
    P2P_PROTO_OK             =  0,
    P2P_PROTO_ERR_SERIALIZE  = -1,  // chyba CBOR serializácie
    P2P_PROTO_ERR_PARSE      = -2,  // chyba CBOR parsovania
    P2P_PROTO_ERR_UNKNOWN    = -3,  // neznámy typ správy
    P2P_PROTO_ERR_RETRY      = -4,  // nedoručené po retry
    P2P_PROTO_ERR_PENDING    = -5,  // pending buffer plný
    P2P_PROTO_ERR_NO_HANDLER = -6,  // chýba handler
} p2p_proto_err_t;
```

---

## Testovací plán (microtest)

### Test 01 – Serializácia a parsovanie správy
- Vytvor `p2p_message_t` s typom `DATA_REQUEST`
- Serializuj cez microcbor
- Parsuj späť
- **Očakávaný výsledok:** všetky polia zhodné

### Test 02 – Dispatch správy
- Príjmi raw paket s typom `GOSSIP`
- Over že sa zavolal network handler
- **Očakávaný výsledok:** správny handler zavolaný

### Test 03 – Retry nedoručenej správy
- Pošli správu uzlu ktorý neodpovedá
- Over že sa retry spustí `retry_count` krát
- **Očakávaný výsledok:** `P2P_EVENT_MSG_FAILED` po vyčerpaní

### Test 04 – Custom správa
- Registruj handler pre `0x80`
- Príjmi správu typu `0x80`
- **Očakávaný výsledok:** custom handler zavolaný

### Test 05 – Broadcast do skupiny
- 3 uzly v rovnakej skupine
- Broadcast správa od uzla A
- **Očakávaný výsledok:** B aj C dostanú správu

### Test 06 – Životný cyklus
- Spusti protokol blok
- Over prechody stavov: BOOT→INIT→STUN→READY
- **Očakávaný výsledok:** správne stavy v správnom poradí

---

## Súbory

```
micronet/
└── src/
    └── protocol/
        ├── p2p_protocol.h
        ├── p2p_protocol.c
        ├── p2p_protocol_dispatch.c
        ├── p2p_protocol_serialize.c
        └── tests/
            └── test_protocol.c
```

---

## Poznámky

- `p2p_protocol_tick()` je jediný tick ktorý musí volať developer – interne volá tick všetkých blokov
- Poradie inicializácie: transport → security → network → data → protocol
- Custom správy 0x80–0xFF sú vždy šifrované rovnako ako systémové správy
- microlog zapisuje každú prijatú a odoslanú správu na nastavenej úrovni
- Na ESP32 odporúčame `max_pending = 8` kvôli RAM
