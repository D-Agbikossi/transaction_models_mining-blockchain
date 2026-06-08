/* ledger.c — UTXO and account-based token balance models */
#include "attendance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Account *find_or_create_account(AccountLedger *ledger, const char *student_id)
{
    size_t i;

    for (i = 0; i < ledger->count; i++) {
        if (strcmp(ledger->accounts[i].student_id, student_id) == 0) {
            return &ledger->accounts[i];
        }
    }

    if (ledger->count >= MAX_STUDENTS) {
        return NULL;
    }

    strncpy(ledger->accounts[ledger->count].student_id, student_id,
            sizeof(ledger->accounts[ledger->count].student_id) - 1);
    ledger->accounts[ledger->count].balance = 0;
    ledger->accounts[ledger->count].nonce = 0;
    ledger->accounts[ledger->count].history = NULL;
    return &ledger->accounts[ledger->count++];
}

static void free_account_history(Account *account)
{
    TxHistoryNode *node = account->history;

    while (node != NULL) {
        TxHistoryNode *next = node->next;
        free(node);
        node = next;
    }
    account->history = NULL;
}

static void account_append_history(Account *account, const char *sender, const char *recipient,
                                   int amount, int fee, int nonce)
{
    TxHistoryNode *node = calloc(1, sizeof(TxHistoryNode));

    if (node == NULL) {
        return;
    }

    strncpy(node->sender, sender, sizeof(node->sender) - 1);
    strncpy(node->recipient, recipient, sizeof(node->recipient) - 1);
    node->amount = amount;
    node->fee = fee;
    node->nonce = nonce;
    node->next = account->history;
    account->history = node;
}

static bool utxo_add(UTXOSet *set, const char *tx_id, int output_index, const char *owner,
                     int amount)
{
    if (set->count >= MAX_UTXOS) {
        fprintf(stderr, "ERROR: UTXO set is full.\n");
        return false;
    }

    strncpy(set->entries[set->count].tx_id, tx_id, HASH_LEN - 1);
    set->entries[set->count].output_index = output_index;
    strncpy(set->entries[set->count].owner, owner, STUDENT_ID_LEN - 1);
    set->entries[set->count].amount = amount;
    set->entries[set->count].spent = false;
    set->count++;
    return true;
}

/* UTXO balance = sum of all unspent outputs owned by student */
static int utxo_balance(const UTXOSet *set, const char *owner)
{
    size_t i;
    int total = 0;

    for (i = 0; i < set->count; i++) {
        if (!set->entries[i].spent && strcmp(set->entries[i].owner, owner) == 0) {
            total += set->entries[i].amount;
        }
    }

    return total;
}

static bool utxo_apply_reward(UTXOSet *set, const Transaction *tx)
{
    int i;

    if (tx->output_count == 0) {
        return true;
    }

    for (i = 0; i < tx->output_count; i++) {
        if (!utxo_add(set, tx->tx_id, i, tx->outputs[i].owner, tx->outputs[i].amount)) {
            return false;
        }
    }

    return true;
}

static bool utxo_mark_spent(UTXOSet *set, const char *utxo_ref)
{
    size_t i;

    for (i = 0; i < set->count; i++) {
        char ref[HASH_LEN + 16];
        snprintf(ref, sizeof(ref), "%s:%d", set->entries[i].tx_id, set->entries[i].output_index);

        if (!set->entries[i].spent && strcmp(ref, utxo_ref) == 0) {
            set->entries[i].spent = true;
            return true;
        }
    }

    return false;
}

/*
 * UTXO transfer: select inputs, pay recipient + fee, return change if needed.
 * Rejects when inputs cannot cover amount + TX_FEE.
 */
static bool utxo_transfer(Ledger *ledger, const char *sender, const char *recipient, int amount)
{
    UTXOSet *set = &ledger->utxo_set;
    Transaction tx;
    int selected_total = 0;
    int needed = amount + TX_FEE;
    size_t i;
    int out_index = 0;

    memset(&tx, 0, sizeof(tx));
    tx.fee = TX_FEE;

    for (i = 0; i < set->count; i++) {
        char ref[HASH_LEN + 16];

        if (set->entries[i].spent || strcmp(set->entries[i].owner, sender) != 0) {
            continue;
        }

        snprintf(ref, sizeof(ref), "%s:%d", set->entries[i].tx_id, set->entries[i].output_index);
        if (tx.input_count >= MAX_TX_INPUTS) {
            break;
        }

        strncpy(tx.inputs[tx.input_count].utxo_ref, ref, sizeof(tx.inputs[tx.input_count].utxo_ref) - 1);
        strncpy(tx.inputs[tx.input_count].owner, sender, sizeof(tx.inputs[tx.input_count].owner) - 1);
        tx.inputs[tx.input_count].amount = set->entries[i].amount;
        selected_total += set->entries[i].amount;
        tx.input_count++;

        if (selected_total >= needed) {
            break;
        }
    }

    if (selected_total < needed) {
        fprintf(stderr, "ERROR: Insufficient balance for transfer (have %d, need %d).\n",
                selected_total, needed);
        return false;
    }

    tx.output_count = 1;
    strncpy(tx.outputs[0].owner, recipient, sizeof(tx.outputs[0].owner) - 1);
    tx.outputs[0].amount = amount;
    out_index = 1;

    /* Excess input value returned to sender as a change UTXO */
    if (selected_total > needed) {
        if (tx.output_count >= MAX_TX_OUTPUTS) {
            fprintf(stderr, "ERROR: Too many outputs.\n");
            return false;
        }
        strncpy(tx.outputs[out_index].owner, sender, sizeof(tx.outputs[out_index].owner) - 1);
        tx.outputs[out_index].amount = selected_total - needed;
        tx.output_count++;
    }

    if (!compute_transaction_id(&tx, tx.tx_id)) {
        return false;
    }

    for (i = 0; i < (size_t)tx.input_count; i++) {
        if (!utxo_mark_spent(set, tx.inputs[i].utxo_ref)) {
            fprintf(stderr, "ERROR: Double-spend prevention triggered.\n");
            return false;
        }
    }

    for (i = 0; i < (size_t)tx.output_count; i++) {
        if (!utxo_add(set, tx.tx_id, (int)i, tx.outputs[i].owner, tx.outputs[i].amount)) {
            return false;
        }
    }

    printf("UTXO transfer confirmed: %s -> %s (%d coins, fee %d).\n", sender, recipient, amount,
           TX_FEE);
    return true;
}

static bool account_apply_reward(AccountLedger *ledger, const Transaction *tx)
{
    Account *account;
    int i;

    for (i = 0; i < tx->output_count; i++) {
        account = find_or_create_account(ledger, tx->outputs[i].owner);
        if (account == NULL) {
            return false;
        }
        account->balance += tx->outputs[i].amount;
        account_append_history(account, "SYSTEM", tx->outputs[i].owner, tx->outputs[i].amount,
                               tx->fee, account->nonce);
    }

    return true;
}

/* Account transfer: requires matching nonce; increments nonce on success */
static bool account_transfer(AccountLedger *ledger, const char *sender, const char *recipient,
                             int amount, int nonce)
{
    Account *sender_account;
    Account *recipient_account;
    int total_debit = amount + TX_FEE;

    sender_account = find_or_create_account(ledger, sender);
    recipient_account = find_or_create_account(ledger, recipient);
    if (sender_account == NULL || recipient_account == NULL) {
        return false;
    }

    if (nonce != sender_account->nonce) {
        fprintf(stderr, "ERROR: Invalid nonce (expected %d, got %d).\n", sender_account->nonce,
                nonce);
        return false;
    }

    if (sender_account->balance < total_debit) {
        fprintf(stderr, "ERROR: Insufficient balance for transfer.\n");
        return false;
    }

    sender_account->balance -= total_debit;
    recipient_account->balance += amount;
    account_append_history(sender_account, sender, recipient, amount, TX_FEE, nonce);
    account_append_history(recipient_account, sender, recipient, amount, TX_FEE, nonce);
    sender_account->nonce++;
    printf("Account transfer confirmed: %s -> %s (%d coins, fee %d, nonce %d).\n", sender,
           recipient, amount, TX_FEE, nonce);
    return true;
}

bool ledger_init(Ledger *ledger, LedgerModel model, const StudentRegistry *registry)
{
    size_t i;

    if (ledger == NULL || registry == NULL) {
        return false;
    }

    memset(ledger, 0, sizeof(*ledger));
    ledger->model = model;

    for (i = 0; i < registry->count; i++) {
        if (model == LEDGER_ACCOUNT) {
            if (find_or_create_account(&ledger->account_ledger, registry->students[i].student_id) ==
                NULL) {
                return false;
            }
        }
    }

    return true;
}

void ledger_cleanup(Ledger *ledger)
{
    size_t i;

    if (ledger == NULL) {
        return;
    }

    if (ledger->model == LEDGER_ACCOUNT) {
        for (i = 0; i < ledger->account_ledger.count; i++) {
            free_account_history(&ledger->account_ledger.accounts[i]);
        }
    }
}

void ledger_set_model(Ledger *ledger, LedgerModel model, const StudentRegistry *registry)
{
    if (ledger == NULL) {
        return;
    }

    ledger_cleanup(ledger);
    ledger_init(ledger, model, registry);
}

LedgerModel ledger_get_model(const Ledger *ledger)
{
    return ledger == NULL ? LEDGER_UTXO : ledger->model;
}

/* Called after mining — credits student tokens from confirmed attendance */
bool ledger_apply_confirmed_block(Ledger *ledger, const Block *block, const Transaction *tx)
{
    if (ledger == NULL || block == NULL || tx == NULL) {
        return false;
    }

    if (block->token_reward <= 0) {
        return true;
    }

    if (ledger->model == LEDGER_UTXO) {
        return utxo_apply_reward(&ledger->utxo_set, tx);
    }

    return account_apply_reward(&ledger->account_ledger, tx);
}

bool ledger_transfer(Ledger *ledger, const char *sender, const char *recipient, int amount)
{
    if (ledger == NULL || sender == NULL || recipient == NULL || amount <= 0) {
        return false;
    }

    if (strcmp(sender, recipient) == 0) {
        fprintf(stderr, "ERROR: Cannot transfer to self.\n");
        return false;
    }

    if (ledger->model == LEDGER_UTXO) {
        return utxo_transfer(ledger, sender, recipient, amount);
    }

    {
        Account *sender_account = find_or_create_account(&ledger->account_ledger, sender);
        if (sender_account == NULL) {
            return false;
        }
        return account_transfer(&ledger->account_ledger, sender, recipient, amount,
                                sender_account->nonce);
    }
}

bool ledger_transfer_with_nonce(Ledger *ledger, const char *sender, const char *recipient,
                                int amount, int nonce)
{
    if (ledger == NULL || ledger->model != LEDGER_ACCOUNT) {
        return false;
    }

    return account_transfer(&ledger->account_ledger, sender, recipient, amount, nonce);
}

int ledger_balance(const Ledger *ledger, const char *student_id)
{
    size_t i;

    if (ledger == NULL || student_id == NULL) {
        return 0;
    }

    if (ledger->model == LEDGER_UTXO) {
        return utxo_balance(&ledger->utxo_set, student_id);
    }

    for (i = 0; i < ledger->account_ledger.count; i++) {
        if (strcmp(ledger->account_ledger.accounts[i].student_id, student_id) == 0) {
            return ledger->account_ledger.accounts[i].balance;
        }
    }

    return 0;
}

bool ledger_credit_miner(Ledger *ledger, const char *miner_id, int amount)
{
    Transaction tx;

    if (ledger == NULL || miner_id == NULL || amount <= 0) {
        return false;
    }

    memset(&tx, 0, sizeof(tx));
    tx.is_reward = true;
    tx.fee = 0;
    tx.output_count = 1;
    strncpy(tx.outputs[0].owner, miner_id, sizeof(tx.outputs[0].owner) - 1);
    tx.outputs[0].amount = amount;
    if (!compute_transaction_id(&tx, tx.tx_id)) {
        return false;
    }

    if (ledger->model == LEDGER_UTXO) {
        return utxo_apply_reward(&ledger->utxo_set, &tx);
    }

    {
        Account *account = find_or_create_account(&ledger->account_ledger, miner_id);
        if (account == NULL) {
            return false;
        }
        account->balance += amount;
        account_append_history(account, "MINER", miner_id, amount, 0, account->nonce);
        return true;
    }
}

void ledger_print_balances(const Ledger *ledger, const StudentRegistry *registry)
{
    size_t i;
    const char *model_name = ledger->model == LEDGER_UTXO ? "UTXO" : "Account";

    printf("\n=== Student Token Balances (%s model) ===\n", model_name);
    for (i = 0; i < registry->count; i++) {
        const Student *student = &registry->students[i];
        printf("  %s (%s): %d coin(s)\n", student->full_name, student->student_id,
               ledger_balance(ledger, student->student_id));
    }
    printf("=========================================\n\n");
}

void ledger_print_utxo_set(const Ledger *ledger)
{
    size_t i;
    int unspent = 0;

    if (ledger == NULL || ledger->model != LEDGER_UTXO) {
        printf("UTXO set display is only available in UTXO model.\n");
        return;
    }

    printf("\n=== Full UTXO Set ===\n");
    for (i = 0; i < ledger->utxo_set.count; i++) {
        const UTXO *utxo = &ledger->utxo_set.entries[i];

        if (utxo->spent) {
            continue;
        }

        unspent++;
        printf("  UTXO %s:%d | owner=%s | amount=%d\n", utxo->tx_id, utxo->output_index,
               utxo->owner, utxo->amount);
    }

    if (unspent == 0) {
        printf("  (no unspent outputs)\n");
    }
    printf("=====================\n\n");
}

void ledger_print_history(const Ledger *ledger, const char *student_id)
{
    size_t i;
    const Account *account = NULL;
    TxHistoryNode *node;
    int entry = 1;

    if (ledger == NULL || ledger->model != LEDGER_ACCOUNT) {
        printf("Transaction history is only available in account model.\n");
        return;
    }

    for (i = 0; i < ledger->account_ledger.count; i++) {
        if (strcmp(ledger->account_ledger.accounts[i].student_id, student_id) == 0) {
            account = &ledger->account_ledger.accounts[i];
            break;
        }
    }

    if (account == NULL) {
        printf("No account found for student ID %s.\n", student_id);
        return;
    }

    printf("\n=== Transaction History for %s ===\n", student_id);
    node = account->history;
    if (node == NULL) {
        printf("  (empty)\n");
    }

    while (node != NULL) {
        printf("  #%d sender=%s recipient=%s amount=%d fee=%d nonce=%d\n", entry++, node->sender,
               node->recipient, node->amount, node->fee, node->nonce);
        node = node->next;
    }
    printf("====================================\n\n");
}
