/* attendance.h — shared types and APIs for attendance, ledger, and mining */
#ifndef ATTENDANCE_H
#define ATTENDANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define MAX_STUDENTS 256
#define MAX_BLOCKS 4096
#define MAX_PENDING 256
#define MAX_UTXOS 8192
#define MAX_TX_INPUTS 16
#define MAX_TX_OUTPUTS 16
#define STUDENT_ID_LEN 20
#define FULL_NAME_LEN 50
#define COURSE_CODE_LEN 10
#define STATUS_LEN 10
#define HASH_LEN 65
#define SIGNATURE_LEN 72
#define GENESIS_PREV_HASH "0000000000000000000000000000000000000000000000000000000000000000"

#define STUDENTS_FILE "students.txt"
#define CHAIN_FILE "attendance_chain.dat"
#define PRIVATE_KEY_FILE "attendance_private.pem"
#define PUBLIC_KEY_FILE "attendance_public.pem"

#define REWARD_PRESENT 10
#define REWARD_LATE 5
#define REWARD_ABSENT 0
#define TX_FEE 1
#define MINING_REWARD 20
#define POOL_FEE_PERCENT 2
#define DEFAULT_DIFFICULTY 2
#define MIN_DIFFICULTY 1
#define MAX_DIFFICULTY 4
#define MAX_POOL_MINERS 8
#define CLOUD_MIN_ROUNDS 1
#define CLOUD_MAX_ROUNDS 5

typedef enum {
    LEDGER_UTXO = 0,
    LEDGER_ACCOUNT = 1
} LedgerModel;

typedef struct {
    char student_id[STUDENT_ID_LEN];
    char full_name[FULL_NAME_LEN];
    char course_code[COURSE_CODE_LEN];
} Student;

/* Block: token_reward, transaction_id, nonce (PoW) */
typedef struct {
    int index;
    time_t timestamp;
    char student_id[STUDENT_ID_LEN];
    char full_name[FULL_NAME_LEN];
    char course_code[COURSE_CODE_LEN];
    char status[STATUS_LEN];
    int token_reward;              /* 10 PRESENT, 5 LATE, 0 ABSENT */
    char transaction_id[HASH_LEN]; /* SHA-256 of reward transaction */
    unsigned long nonce;           /* incremented during proof-of-work */
    char previous_hash[HASH_LEN];
    unsigned char signature[SIGNATURE_LEN];
    char hash[HASH_LEN];
} Block;

typedef struct {
    Block blocks[MAX_BLOCKS];
    size_t length;
} Blockchain;

/* Blocks wait here until a miner confirms them */
typedef struct {
    Block blocks[MAX_PENDING];
    size_t count;
} PendingPool;

typedef struct {
    char tx_id[HASH_LEN];
    char utxo_ref[HASH_LEN];
    char owner[STUDENT_ID_LEN];
    int amount;
} TxInput;

typedef struct {
    char owner[STUDENT_ID_LEN];
    int amount;
} TxOutput;

typedef struct {
    char tx_id[HASH_LEN];
    TxInput inputs[MAX_TX_INPUTS];
    int input_count;
    TxOutput outputs[MAX_TX_OUTPUTS];
    int output_count;
    int fee;
    bool is_reward;
} Transaction;

typedef struct {
    char tx_id[HASH_LEN];
    int output_index;
    char owner[STUDENT_ID_LEN];
    int amount;
    bool spent;
} UTXO;

typedef struct {
    UTXO entries[MAX_UTXOS];
    size_t count;
} UTXOSet;

/* Account model: per-student transaction log as a linked list */
typedef struct TxHistoryNode {
    char sender[STUDENT_ID_LEN];
    char recipient[STUDENT_ID_LEN];
    int amount;
    int fee;
    int nonce;
    struct TxHistoryNode *next;
} TxHistoryNode;

typedef struct {
    char student_id[STUDENT_ID_LEN];
    int balance;
    int nonce;
    TxHistoryNode *history;
} Account;

typedef struct {
    Account accounts[MAX_STUDENTS];
    size_t count;
} AccountLedger;

typedef struct {
    LedgerModel model;
    UTXOSet utxo_set;
    AccountLedger account_ledger;
} Ledger;

typedef struct {
    Student students[MAX_STUDENTS];
    size_t count;
} StudentRegistry;

/* students.c */
bool load_student_registry(StudentRegistry *registry, const char *path);
const Student *find_student(const StudentRegistry *registry, const char *student_id);
bool is_valid_status(const char *status);

/* crypto.c */
bool crypto_init_keys(void);
void crypto_cleanup(void);
bool compute_block_hash(const Block *block, char *out_hash);
bool sign_block(Block *block);
bool verify_block_signature(const Block *block);
size_t signature_length(const unsigned char *signature, size_t max_len);
bool sha256_hex(const char *payload, char *out_hash);

/* transactions.c */
int token_reward_for_status(const char *status);
bool create_reward_transaction(const Block *block, Transaction *tx);
bool compute_transaction_id(const Transaction *tx, char *out_id);
bool serialize_transaction(const Transaction *tx, char *buffer, size_t buffer_size);

/* pending.c */
void pending_init(PendingPool *pool);
bool pending_add(PendingPool *pool, const Block *block);
bool pending_is_empty(const PendingPool *pool);
void pending_print(const PendingPool *pool);
size_t pending_count(const PendingPool *pool);
bool pending_get(const PendingPool *pool, size_t index, Block *out);
void pending_clear(PendingPool *pool);
bool pending_remove_front(PendingPool *pool, Block *out);

/* blockchain.c */
bool blockchain_init(Blockchain *chain);
bool blockchain_load(Blockchain *chain, const char *path);
bool blockchain_save(const Blockchain *chain, const char *path);
bool blockchain_append_mined_block(Blockchain *chain, const Block *block);
bool blockchain_validate(const Blockchain *chain, int difficulty);
void blockchain_print_records(const Blockchain *chain);
bool blockchain_tamper_demo(Blockchain *chain, int block_index);
const Block *blockchain_tip(const Blockchain *chain);

/* ledger.c */
bool ledger_init(Ledger *ledger, LedgerModel model, const StudentRegistry *registry);
void ledger_cleanup(Ledger *ledger);
void ledger_set_model(Ledger *ledger, LedgerModel model, const StudentRegistry *registry);
LedgerModel ledger_get_model(const Ledger *ledger);
bool ledger_apply_confirmed_block(Ledger *ledger, const Block *block, const Transaction *tx);
bool ledger_transfer(Ledger *ledger, const char *sender, const char *recipient, int amount);
bool ledger_transfer_with_nonce(Ledger *ledger, const char *sender, const char *recipient,
                                int amount, int nonce);
int ledger_balance(const Ledger *ledger, const char *student_id);
void ledger_print_balances(const Ledger *ledger, const StudentRegistry *registry);
void ledger_print_utxo_set(const Ledger *ledger);
void ledger_print_history(const Ledger *ledger, const char *student_id);
bool ledger_credit_miner(Ledger *ledger, const char *miner_id, int amount);

/* mining.c */
bool hash_meets_difficulty(const char *hash, int difficulty);
bool mine_block_pow(Block *block, int difficulty, unsigned long *attempts);
bool mining_solo(Blockchain *chain, PendingPool *pool, Ledger *ledger, int difficulty,
                 const char *miner_id);
bool mining_pool(Blockchain *chain, PendingPool *pool, Ledger *ledger, int difficulty,
                 int miner_count);
bool mining_cloud(Blockchain *chain, PendingPool *pool, Ledger *ledger, int difficulty,
                  int rounds, int rental_fee, int maintenance_fee);

/* attendance flow */
bool mark_attendance(PendingPool *pool, const Blockchain *chain, const StudentRegistry *registry,
                     const char *student_id, const char *status);

#endif
