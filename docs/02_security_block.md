# Blok 02 – Bezpečnosť

**Projekt:** micronet
**Licencia:** MIT
**Cieľová platforma:** ESP32, Linux, Windows
**Jazyk:** C99
**Závislosti:** microcrypt, microdb_secure, microfsm

---

## Účel

Bezpečnostný blok zabezpečuje šifrovanie všetkej komunikácie, autentizáciu uzlov a správu kľúčov. Sedí medzi transport blokom (raw pakety) a sieťovým blokom (uzly, skupiny). Žiadny vyšší blok nikdy nevidí nešifrované dáta na sieti.

---

## Zodpovednosti

- Generovanie kľúčového páru uzla (verejný + súkromný)
- Handshake medzi dvoma uzlami – výmena verejných kľúčov
- Šifrovanie odchádzajúcich paketov (AES-128-CBC)
- Dešifrovanie prichádzajúcich paketov
- HMAC-SHA256 overenie integrity paketu
- Bezpečné ukladanie kľúčov (microdb_secure)
- Správa skupinových kľúčov

---

## Čo bezpečnostný blok NERIEŠI

- Transport (sockety, IP) – to je transport blok
- Identita uzlov, skupiny – to je sieťový blok
- Logika príkazov – to je protokol blok

---

## Kľúčová architektúra

Každý uzol má:
- **node_privkey** – súkromný kľúč (nikdy neopustí zariadenie)
- **node_pubkey** – verejný kľúč (zdieľa sa so všetkými)
- **session_key** – dočasný symetrický kľúč pre každé spojenie
- **group_key[]** – šifrovacie kľúče skupín ktorých je členom

```
Uzol A                          Uzol B
  │                               │
  │── [pubkey_A] ────────────────►│
  │◄─────────────────── [pubkey_B]│
  │                               │
  │  session_key = derive(privkey_A, pubkey_B)
  │                               │
  │══ [šifrované dáta] ══════════►│
```

---

## Konfigurácia

```c
typedef struct {
    uint8_t  node_privkey[32];     // súkromný kľúč uzla
    uint8_t  node_pubkey[32];      // verejný kľúč uzla
    uint8_t  group_keys[P2P_MAX_GROUPS][16];  // skupinové kľúče
    uint8_t  group_count;          // počet skupín
    bool     store_keys;           // uložiť kľúče do microdb_secure
} p2p_security_config_t;
```

---

## Interné dátové štruktúry

```c
typedef struct {
    uint8_t session_key[16];       // AES-128 session kľúč
    uint8_t remote_pubkey[32];     // verejný kľúč druhej strany
    bool    authenticated;         // handshake dokončený
    uint32_t established_at;       // timestamp nadviazania
} p2p_session_t;

typedef struct {
    uint8_t          node_privkey[32];
    uint8_t          node_pubkey[32];
    p2p_session_t    sessions[P2P_MAX_SESSIONS];
    uint8_t          group_keys[P2P_MAX_GROUPS][16];
    uint8_t          group_count;
    microfsm_t       fsm;
} p2p_security_t;
```

---

## Šifrovaný paket

Každý paket zo transport bloku je obalený:

```
[ hmac 32B ][ iv 16B ][ encrypted_payload ... ]
```

| Pole              | Veľkosť | Popis                          |
|-------------------|---------|--------------------------------|
| hmac              | 32 B    | HMAC-SHA256 celého paketu      |
| iv                | 16 B    | náhodný inicializačný vektor   |
| encrypted_payload | N B     | AES-128-CBC zašifrované dáta   |

---

## Handshake protokol

```
A                              B
│                              │
│──── HELLO [pubkey_A] ───────►│
│◄─── HELLO [pubkey_B] ────────│
│                              │
│  Obaja vypočítajú session_key = HMAC(pubkey_A XOR pubkey_B)
│                              │
│──── VERIFY [hmac_A] ────────►│  (overenie že obaja majú rovnaký session_key)
│◄─── VERIFY [hmac_B] ─────────│
│                              │
│══════ spojenie aktívne ══════│
```

---

## API

```c
// Inicializácia – generuje kľúčový pár ak neexistuje
p2p_err_t p2p_security_init(p2p_security_t *ctx, const p2p_security_config_t *cfg);

// Získanie vlastného verejného kľúča
p2p_err_t p2p_security_get_pubkey(p2p_security_t *ctx, uint8_t pubkey[32]);

// Spustenie handshake s uzlom
p2p_err_t p2p_security_handshake(p2p_security_t *ctx, const uint8_t remote_pubkey[32]);

// Overenie či je spojenie s uzlom autentizované
bool p2p_security_is_authenticated(p2p_security_t *ctx, const uint8_t remote_pubkey[32]);

// Šifrovanie paketu pred odoslaním
p2p_err_t p2p_security_encrypt(p2p_security_t *ctx, const uint8_t remote_pubkey[32],
                                const uint8_t *plain, size_t plain_len,
                                uint8_t *out, size_t *out_len);

// Dešifrovanie prijatého paketu
p2p_err_t p2p_security_decrypt(p2p_security_t *ctx, const uint8_t remote_pubkey[32],
                                const uint8_t *cipher, size_t cipher_len,
                                uint8_t *out, size_t *out_len);

// Šifrovanie skupinovej správy
p2p_err_t p2p_security_encrypt_group(p2p_security_t *ctx, uint8_t group_idx,
                                      const uint8_t *plain, size_t plain_len,
                                      uint8_t *out, size_t *out_len);

// Dešifrovanie skupinovej správy
p2p_err_t p2p_security_decrypt_group(p2p_security_t *ctx, uint8_t group_idx,
                                      const uint8_t *cipher, size_t cipher_len,
                                      uint8_t *out, size_t *out_len);

// Pridanie skupinového kľúča (pri pozvaní do skupiny)
p2p_err_t p2p_security_add_group_key(p2p_security_t *ctx, const uint8_t group_key[16]);

// Uvoľnenie zdrojov
void p2p_security_deinit(p2p_security_t *ctx);
```

---

## Stavový automat (microfsm)

```
IDLE
 └─► HANDSHAKE_HELLO
       └─► HANDSHAKE_VERIFY
             ├─► AUTHENTICATED  →  (spojenie aktívne)
             └─► FAILED         →  IDLE
```

---

## Chybové kódy

```c
typedef enum {
    P2P_SEC_OK              =  0,
    P2P_SEC_ERR_KEYGEN      = -1,  // chyba generovania kľúča
    P2P_SEC_ERR_HANDSHAKE   = -2,  // handshake zlyhal
    P2P_SEC_ERR_HMAC        = -3,  // HMAC overenie zlyhalo – paket poškodený alebo falzifikát
    P2P_SEC_ERR_DECRYPT     = -4,  // dešifrovanie zlyhalo
    P2P_SEC_ERR_NO_SESSION  = -5,  // neexistuje session pre daný uzol
    P2P_SEC_ERR_NO_GROUP    = -6,  // neznámy skupinový kľúč
    P2P_SEC_ERR_BUF         = -7,  // buffer príliš malý
} p2p_sec_err_t;
```

---

## Testovací plán (microtest)

### Test 01 – Generovanie kľúčového páru
- Inicializuj security kontext bez existujúcich kľúčov
- Over že `node_pubkey` nie je samé nuly
- Over že `node_privkey != node_pubkey`
- **Očakávaný výsledok:** `P2P_SEC_OK`, unikátné kľúče

### Test 02 – Handshake medzi dvoma uzlami
- Vytvor dva security kontexty A a B
- Vymeň pubkey, vykonaj handshake
- Over že obaja majú rovnaký `session_key`
- **Očakávaný výsledok:** `P2P_SEC_OK`, `is_authenticated == true`

### Test 03 – Šifrovanie a dešifrovanie
- Zašifruj správu z A pre B
- Dešifruj na strane B
- Over zhodnosť dát
- **Očakávaný výsledok:** dáta zhodné

### Test 04 – Detekcia falzifikátu (HMAC)
- Zašifruj správu, pozmeň 1 bajt v ciphertext
- Pokús sa dešifrovať
- **Očakávaný výsledok:** `P2P_SEC_ERR_HMAC`

### Test 05 – Skupinové šifrovanie
- Pridaj rovnaký group_key do dvoch kontextov
- Zašifruj skupinovú správu
- Dešifruj na druhej strane
- **Očakávaný výsledok:** dáta zhodné

### Test 06 – Neznáma skupina
- Pokús sa dešifrovať skupinovú správu bez group_key
- **Očakávaný výsledok:** `P2P_SEC_ERR_NO_GROUP`

### Test 07 – Persistent kľúče
- Inicializuj s `store_keys = true`
- Deinicializuj a znovu inicializuj
- Over že `node_pubkey` je rovnaký
- **Očakávaný výsledok:** kľúče zachované cez reštart

---

## Súbory

```
micronet/
└── src/
    └── security/
        ├── p2p_security.h
        ├── p2p_security.c
        ├── p2p_security_handshake.c
        ├── p2p_security_keys.c
        └── tests/
            └── test_security.c
```

---

## Poznámky

- Súkromný kľúč **nikdy** neopustí zariadenie – pri handshake sa posiela len verejný kľúč
- IV je generovaný náhodne pre každý paket – nikdy sa neopakuje
- Na ESP32 použiť hardvérový RNG pre generovanie kľúčov a IV
- Session kľúče sa neukladajú – pri reštarte prebehne nový handshake
- Skupinové kľúče sa ukladajú šifrovane cez microdb_secure
