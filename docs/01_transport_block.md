# Blok 01 – Transport

**Projekt:** p2plib  
**Licencia:** MIT  
**Cieľová platforma:** ESP32, Linux, Windows  
**Jazyk:** C99  
**Závislosti:** micoring, microcodec, microres, microtimer

---

## Účel

Transport blok je najnižšia aktívna vrstva knižnice. Zabezpečuje fyzický prenos dát medzi dvoma uzlami cez internet (UDP), zistenie vonkajšej IP cez STUN a základný buffering paketov. Všetky vyššie bloky komunikujú výhradne cez transport blok – nikdy priamo so socketmi.

---

## Zodpovednosti

- Otvorenie a správa UDP socketu
- Zistenie vonkajšej IP a portu cez STUN
- Odoslanie a príjem raw paketov
- Buffering odchádzajúcich a prichádzajúcich paketov (micoring)
- Kompresia paketov pred odoslaním (microcodec)
- Retry pri strate paketu (microres)
- Heartbeat timer – detekcia výpadku spojenia (microtimer)

---

## Čo transport blok NERIEŠI

- Šifrovanie – to je bezpečnostný blok
- Identita uzlov – to je sieťový blok
- Obsah paketov – to je protokol blok

---

## Konfigurácia

```c
typedef struct {
    const char *stun_host;       // napr. "stun.l.google.com"
    uint16_t    stun_port;       // napr. 19302
    uint16_t    local_port;      // lokálny UDP port (0 = automaticky)
    uint32_t    heartbeat_ms;    // interval heartbeat v ms (napr. 5000)
    uint32_t    timeout_ms;      // timeout spojenia v ms (napr. 15000)
    uint8_t     retry_count;     // počet pokusov pri strate paketu
    uint32_t    retry_delay_ms;  // oneskorenie medzi pokusmi
    size_t      rx_buf_size;     // veľkosť prijímacieho buffera
    size_t      tx_buf_size;     // veľkosť odosielacieho buffera
} p2p_transport_config_t;
```

---

## Interné dátové štruktúry

```c
typedef struct {
    uint8_t  data[P2P_MAX_PACKET_SIZE];
    size_t   len;
    uint32_t timestamp;
    uint8_t  remote_ip[4];
    uint16_t remote_port;
} p2p_packet_t;

typedef struct {
    int          sock_fd;
    uint8_t      external_ip[4];
    uint16_t     external_port;
    bool         stun_resolved;
    micoring_t   rx_ring;
    micoring_t   tx_ring;
    microtimer_t heartbeat_timer;
    microtimer_t timeout_timer;
    microres_t   retry_ctx;
} p2p_transport_t;
```

---

## Paketová hlavička

Každý odoslaný paket má hlavičku pred dátami:

```
[ magic 2B ][ version 1B ][ flags 1B ][ seq 2B ][ len 2B ][ payload ... ]
```

| Pole    | Veľkosť | Popis                              |
|---------|---------|------------------------------------|
| magic   | 2 B     | 0xP2 0xLB – identifikácia protokolu |
| version | 1 B     | verzia protokolu (aktuálne 0x01)   |
| flags   | 1 B     | ACK / HEARTBEAT / COMPRESSED / ... |
| seq     | 2 B     | poradové číslo paketu              |
| len     | 2 B     | dĺžka payload v bajtoch            |
| payload | N B     | dáta (môžu byť komprimované)       |

---

## API

```c
// Inicializácia transport bloku
p2p_err_t p2p_transport_init(p2p_transport_t *ctx, const p2p_transport_config_t *cfg);

// Zistenie vonkajšej IP cez STUN
p2p_err_t p2p_transport_stun_resolve(p2p_transport_t *ctx);

// Získanie vlastnej vonkajšej IP a portu
p2p_err_t p2p_transport_get_external_addr(p2p_transport_t *ctx, uint8_t ip[4], uint16_t *port);

// Odoslanie paketu na cieľovú adresu
p2p_err_t p2p_transport_send(p2p_transport_t *ctx, const uint8_t ip[4], uint16_t port,
                              const uint8_t *data, size_t len);

// Príjem paketu (neblokujúci)
p2p_err_t p2p_transport_recv(p2p_transport_t *ctx, p2p_packet_t *out_packet);

// Tick – volaj periodicky (napr. každých 10ms) pre heartbeat a retry
p2p_err_t p2p_transport_tick(p2p_transport_t *ctx);

// Uvoľnenie zdrojov
void p2p_transport_deinit(p2p_transport_t *ctx);
```

---

## Stavový automat (microfsm)

```
IDLE
 └─► STUN_RESOLVING  →  STUN_DONE
                              └─► LISTENING
                                    ├─► SENDING
                                    ├─► RECEIVING
                                    └─► TIMEOUT → IDLE
```

---

## Chybové kódy

```c
typedef enum {
    P2P_OK              =  0,
    P2P_ERR_SOCK        = -1,  // chyba socketu
    P2P_ERR_STUN        = -2,  // STUN zlyhalo
    P2P_ERR_TIMEOUT     = -3,  // timeout spojenia
    P2P_ERR_RETRY       = -4,  // vyčerpané pokusy
    P2P_ERR_BUF_FULL    = -5,  // buffer plný
    P2P_ERR_BAD_PACKET  = -6,  // neplatná hlavička
    P2P_ERR_INVALID_ARG = -7,  // neplatný argument
} p2p_err_t;
```

---

## Testovací plán (microtest)

### Test 01 – STUN resolve
- Inicializuj transport s `stun.l.google.com:19302`
- Zavolaj `p2p_transport_stun_resolve()`
- Over že `external_ip` nie je `0.0.0.0` a port nie je 0
- **Očakávaný výsledok:** `P2P_OK`, platná vonkajšia IP

### Test 02 – Odoslanie a príjem paketu (loopback)
- Otvor dva transport kontexty na `127.0.0.1`
- Pošli paket z A na B
- Over že B prijal správne dáta, správnu dĺžku, platné seq číslo
- **Očakávaný výsledok:** `P2P_OK`, dáta zhodné

### Test 03 – Kompresia
- Pošli paket s opakovateľnými dátami (RLE vhodné)
- Over že flag `COMPRESSED` je nastavený
- Over že príjemca dešifroval správne
- **Očakávaný výsledok:** dáta zhodné, menší paket

### Test 04 – Retry pri strate
- Simuluj stratu paketu (zahoď každý druhý)
- Over že retry mechanizmus doručí paket
- **Očakávaný výsledok:** `P2P_OK` po retry

### Test 05 – Heartbeat timeout
- Inicializuj s `timeout_ms = 1000`
- Nevolaj tick 2 sekundy
- Over že stav prejde do `TIMEOUT`
- **Očakávaný výsledok:** `P2P_ERR_TIMEOUT`

### Test 06 – Buffer full
- Naplň RX ring buffer
- Pokús sa prijať ďalší paket
- **Očakávaný výsledok:** `P2P_ERR_BUF_FULL`

---

## Platformové HAL

Pre prenositeľnosť medzi ESP32 a Linux/Windows sú socket operácie oddelené do HAL vrstvy:

```c
typedef struct {
    int  (*sock_open)(uint16_t port);
    void (*sock_close)(int fd);
    int  (*sock_send)(int fd, const uint8_t *ip, uint16_t port,
                      const uint8_t *data, size_t len);
    int  (*sock_recv)(int fd, uint8_t *ip, uint16_t *port,
                      uint8_t *buf, size_t buf_len);
    uint32_t (*now_ms)(void);
} p2p_hal_t;
```

ESP32 a Linux implementujú túto štruktúru zvlášť. Zvyšok kódu je spoločný.

---

## Súbory

```
p2plib/
└── src/
    └── transport/
        ├── p2p_transport.h
        ├── p2p_transport.c
        ├── p2p_transport_stun.c
        ├── p2p_hal.h
        ├── p2p_hal_linux.c
        ├── p2p_hal_esp32.c
        └── tests/
            └── test_transport.c
```

---

## Poznámky

- `p2p_transport_tick()` musí byť volaný pravidelne – ideálne z hlavnej slučky alebo timera
- Na ESP32 odporúčame volať tick z FreeRTOS tasku každých 10 ms
- Maximálna veľkosť paketu `P2P_MAX_PACKET_SIZE` je konfigurovateľná pri kompilácii (default 512 B)
- STUN resolve sa volá pri štarte a periodicky každých 60 sekúnd (NAT binding refresh)
