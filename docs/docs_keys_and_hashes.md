# Kľúče a hashe – kompletná špecifikácia

**Projekt:** p2plib  
**Dokument:** docs/keys_and_hashes.md

---

## Prehľad – čo v systéme existuje

| Názov | Veľkosť | Typ | Kde žije | Na čo slúži |
|-------|---------|-----|----------|-------------|
| `node_privkey` | 32 B | súkromný | len v zariadení | podpisovanie, derivácia session kľúča |
| `node_pubkey` | 32 B | verejný | zdieľa sa voľne | identita uzla, šifrovanie pre uzol |
| `session_key` | 16 B | symetrický | RAM, dočasný | AES šifrovanie jedného spojenia |
| `group_key` | 16 B | symetrický | flash (šifrovaný) | AES šifrovanie skupinovej komunikácie |
| `group_hash` | 16 B | verejný | DB, zdieľa sa | identifikátor skupiny |
| `node_id` | 32 B | verejný | DB | = node_pubkey, adresa uzla |
| `msg_hmac` | 32 B | odvodený | hlavička paketu | overenie integrity správy |
| `db_version` | 4 B | uint32 | DB záznam | verzia pre synchronizáciu |

---

## 1. Node kľúčový pár

### Generovanie

```
node_privkey  ←  CSPRNG(32 bajtov)
node_pubkey   ←  Curve25519(node_privkey)
node_id       =  node_pubkey
```

- Generuje sa **raz pri prvom štarte**
- `node_privkey` sa uloží šifrovane cez `microdb_secure`
- `node_pubkey` je verejná identita uzla – môže sa voľne zdieľať
- Na ESP32 použiť hardvérový RNG (`esp_fill_random()`)

### Ako vyzerá v praxi

```
node_privkey: a3f2b1c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2
node_pubkey:  4a7b3c8d2e9f1a6b5c0d4e8f3a2b7c1d6e5f0a9b4c3d8e2f7a1b6c5d0e4f3a8
node_id:      (rovnaký ako node_pubkey)
```

---

## 2. Session kľúč

Dočasný symetrický kľúč pre každé P2P spojenie. Derivuje sa počas handshake.

### Derivácia

```
shared_secret  =  X25519(my_privkey, remote_pubkey)
session_key    =  HMAC-SHA256(shared_secret, node_pubkey_A || node_pubkey_B)[0..15]
```

- Obaja uzly derivujú **rovnaký** `session_key` nezávisle – bez prenosu kľúča
- Platí len počas jedného spojenia – po reštarte nový handshake
- Nikdy sa neukladá

### Ako vyzerá v praxi

```
shared_secret:  8f1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1
session_key:    3d4e5f6a7b8c9d0e  (16 bajtov, AES-128)
```

---

## 3. Group kľúč a group hash

### Generovanie group_key

```
group_key  ←  CSPRNG(16 bajtov)
```

### Derivácia group_hash

```
group_hash  =  HMAC-SHA256(group_key, "p2plib_group" || created_at)[0..15]
```

- `group_hash` je **verejný** – identifikuje skupinu, nezdrádzuje kľúč
- `group_key` je **tajný** – len členovia skupiny ho poznajú
- Rovnaký `group_key` vždy dá rovnaký `group_hash` – deterministický

### Ako vyzerá v praxi

```
group_key:   a1b2c3d4e5f6a7b8  (16 bajtov)
group_hash:  9f8e7d6c5b4a3f2e  (16 bajtov, verejný)
```

### Prenos group_key pri pozvánke

Pri pozvaní nového člena sa `group_key` **nikdy neposiela otvorene**:

```
encrypted_group_key  =  AES-128-CBC(session_key, group_key)
```

Príjemca dešifruje pomocou svojho `session_key` s pozývateľom.

---

## 4. HMAC – overenie integrity paketu

Každý odoslaný paket obsahuje HMAC na začiatku:

### Výpočet

```
hmac  =  HMAC-SHA256(session_key, version || flags || seq || len || payload)
```

- Počíta sa zo **všetkých polí hlavičky aj payload**
- Príjemca vypočíta HMAC a porovná – ak nesedí, paket zahodí
- Chráni pred modifikáciou aj falzifikátom

### Ako vyzerá v praxi

```
hmac: 7a3f2b1c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2
      (32 bajtov, prvých 32 bajtov každého paketu)
```

---

## 5. Formát kompletného paketu na sieti

```
┌─────────────────────────────────────────────────────────────┐
│  HMAC-SHA256        32 B  overenie integrity                │
│  IV                 16 B  náhodný, pre každý paket nový     │
├─────────────────────────────────────────────────────────────┤
│  ŠIFROVANÉ (AES-128-CBC):                                   │
│    magic            2 B   0xP2 0xLB                         │
│    version          1 B   0x01                              │
│    flags            1 B   ACK|HB|COMPRESSED|GROUP           │
│    seq              2 B   poradové číslo                    │
│    len              2 B   dĺžka payload                     │
│    src_node_id     32 B   odosielateľ                       │
│    dst_node_id     32 B   príjemca (0x00..00 = broadcast)   │
│    group_hash      16 B   skupina (0x00..00 = žiadna)        │
│    payload          N B   CBOR správa                       │
└─────────────────────────────────────────────────────────────┘
```

**Celková réžia:** 48 B fixná (HMAC + IV) + 88 B hlavička + payload

---

## 6. CBOR payload – formát správ

Každý payload je CBOR mapa. Príklady:

### HELLO (handshake)

```cbor
{
  "t":   1,                      // P2P_MSG_HELLO
  "id":  1042,                   // message ID
  "ts":  1700000000,             // timestamp
  "pk":  h'4a7b3c8d...',        // node_pubkey (32 B)
  "nm":  "sensor_01"            // node_name (voliteľné)
}
```

### DATA_REQUEST

```cbor
{
  "t":   32,                     // P2P_MSG_DATA_REQUEST
  "id":  1043,
  "ts":  1700000001,
  "rid": 55,                     // request ID pre spárovanie odpovede
  "k":   "temperature"          // kľúč
}
```

### DATA_RESPONSE

```cbor
{
  "t":   33,                     // P2P_MSG_DATA_RESPONSE
  "id":  1044,
  "ts":  1700000001,
  "rid": 55,                     // rovnaký request ID
  "k":   "temperature",
  "v":   23.5,                   // hodnota
  "vt":  1                       // value type (0=bytes, 1=float, 2=int, 3=string)
}
```

### GOSSIP

```cbor
{
  "t":   16,                     // P2P_MSG_GOSSIP
  "id":  1045,
  "ts":  1700000002,
  "nodes": [
    {
      "id":  h'4a7b3c8d...',    // node_id
      "ip":  h'c0a80101',       // IP (4 B)
      "pt":  51820,             // port
      "by":  h'9f8e7d6c...',    // invited_by node_id
      "ver": 42                 // db_version
    }
  ]
}
```

### GROUP_INVITE

```cbor
{
  "t":   19,                     // P2P_MSG_GROUP_INVITE
  "id":  1046,
  "ts":  1700000003,
  "gh":  h'9f8e7d6c5b4a3f2e',  // group_hash (16 B)
  "ek":  h'3d4e5f6a...',        // encrypted group_key (16 B)
  "by":  h'4a7b3c8d...'         // pozývateľ node_id
}
```

---

## 7. Ukladanie kľúčov

| Kľúč | Kde | Ako |
|------|-----|-----|
| `node_privkey` | flash | microdb_secure (AES-128 + HMAC) |
| `node_pubkey` | flash | microdb plaintext |
| `group_key[]` | flash | microdb_secure (AES-128 + HMAC) |
| `session_key` | RAM | nikdy neuložený |
| `group_hash[]` | flash | microdb plaintext |

---

## 8. Čo vidíš pri debugovaní (UART)

```
p2p status
──────────────────────────────────────────────
node_id:     4a7b3c8d2e9f1a6b...  (skrátený)
external_ip: 85.160.42.11:51820
nodes:       3 known, 2 online
groups:      2
uptime:      1h 23m 45s
──────────────────────────────────────────────

p2p nodes
──────────────────────────────────────────────
[0] 4a7b3c8d...  85.160.42.11  ONLINE   group: home
[1] 9f8e7d6c...  82.119.33.77  ONLINE   group: home, work
[2] 3b2a1f0e...  91.200.12.44  OFFLINE  last: 5m ago
──────────────────────────────────────────────

p2p packet (pri log_level=4)
──────────────────────────────────────────────
TX → 9f8e7d6c...
  type:  DATA_REQUEST (0x20)
  msg_id: 1043
  key:   "temperature"
  size:  97 B (48 hmac+iv + 49 enc)
──────────────────────────────────────────────
```

---

## 9. Súhrn – čo kde hľadať pri debugovaní

| Problém | Čo skontrolovať |
|---------|----------------|
| Handshake zlyháva | `node_pubkey` na oboch stranách, `session_key` derivácia |
| Paket zahodený | `msg_hmac` – poškodený paket alebo zlý `session_key` |
| Skupina nefunguje | `group_hash` musí byť rovnaký, `group_key` musí byť rovnaký |
| Uzol nenájdený | `group_hash` v gossip správe, `db_version` synchronizácia |
| Dešifrovanie zlyhá | `IV` – skontroluj či sa prenáša celý, `session_key` platnosť |
