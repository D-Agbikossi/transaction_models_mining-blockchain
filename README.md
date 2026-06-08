# Blockchain Attendance System

A C implementation of an immutable attendance ledger using a blockchain, SHA-256 hashing, and ECDSA digital signatures. Student IDs are validated against a registry before any record is written. With token reward transactions, a pending pool, UTXO and account-based ledger models**, and solo / pool / cloud mining simulations.

## Dependencies

- GCC (C11)
- OpenSSL development libraries (`libssl-dev` on Debian/Ubuntu)

```bash
sudo apt install build-essential libssl-dev
```

## Compilation

```bash
make
```

Clean build artifacts and generated chain/keys:

```bash
make clean
```

## Running

```bash
./attendance
# or
make run
```

### Command-line options

| Option            | Description                                                                    |
|-------------------|--------------------------------------------------------------------------------|
| `--model utxo`    | Use UTXO ledger (default)                                                      |
| `--model account` | Use account-based ledger                                                       |
| `--difficulty N`  | PoW difficulty: hash must start with N leading `0` characters (1–4, default 2) |

Example:

```bash
./attendance --model account --difficulty 2
```

## Switching transaction models

- **At startup:** `--model utxo` or `--model account`
- **During session:** `model utxo` or `model account` (resets in-memory ledger)

## Setting mining difficulty

- **At startup:** `--difficulty 3`
- **During session:** `difficulty 3` (valid range: 1–4)

Higher difficulty requires more hash attempts and longer confirmation time.

## Essential commands

| Command                                | Purpose                                                       |
|----------------------------------------|---------------------------------------------------------------|
| `mark <id> <PRESENT\|ABSENT\|LATE>`    | Queue attendance; creates token tx for PRESENT (10) / LATE (5)|
| `pending`                              | Show unconfirmed blocks                                       |
| `mine solo`                            | Solo PoW mining                                               |
| `mine pool [miners]`                   | Pool mining with reward sharing                               |
| `mine cloud <rounds> <rental> <maint>` | Cloud mining (1–5 rounds)                                     |
| `view`                                 | Confirmed attendance records                                  |
| `balances` / `balance <id>`            | Token balances                                                |
| `utxos`                                | Full UTXO set (UTXO model)                                    |
| `transfer <from> <to> <amount>`        | Manual token transfer                                         |
| `history <id>`                         | Transaction history (account model)                           |
| `validate`                             | Verify chain integrity                                        |

## Testing mining simulations

Automated smoke tests:

```bash
make test
```

### Manual demo flow

```text
attendance> mark ALU001 PRESENT
attendance> mark ALU002 LATE
attendance> mark ALU003 ABSENT
attendance> pending
attendance> mine solo
attendance> balances
attendance> utxos
attendance> view
```

Pool mining:

```text
attendance> mark ALU001 PRESENT
attendance> mine pool 5
```

Cloud mining (unprofitable example):

```text
attendance> mark ALU001 PRESENT
attendance> mine cloud 3 500 200
```

### Edge cases to verify

- **ABSENT:** no transaction ID, zero token reward
- **Insufficient balance:** `transfer ALU001 ALU002 999` before earning tokens
- **Failed nonce (account model):** start with `--model account`, earn tokens, then test invalid nonce via code/API
- **Unprofitable cloud rental:** high rental + maintenance fees vs. mining reward

## Design notes

- Attendance blocks enter a **pending pool** until mined.
- **PRESENT** → 10 coins, **LATE** → 5 coins, **ABSENT** → no transaction.
- A **1-coin fee** is deducted from reward transactions before crediting students.
- **Mining reward:** 20 coins per confirmed block to the miner (solo/pool/cloud).
- PoW: increment `nonce` until SHA-256 block hash has N leading zero hex characters.

## Project Structure

```
attendance.h      Shared types and API
main.c            CLI
students.c        Student registry
blockchain.c      Confirmed chain
pending.c         Pending pool + mark flow
transactions.c    Reward transaction creation
ledger.c          UTXO and account models
mining.c          Solo, pool, cloud mining
crypto.c          SHA-256 + ECDSA
students.txt      Sample student registry
```

## Author

Denaton Agbikossi
