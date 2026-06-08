/* transactions.c — token reward transactions and transaction ID hashing */
#include "attendance.h"

#include <stdio.h>
#include <string.h>

/* Map attendance status to coin reward */
int token_reward_for_status(const char *status)
{
    if (strcmp(status, "PRESENT") == 0) {
        return REWARD_PRESENT;
    }
    if (strcmp(status, "LATE") == 0) {
        return REWARD_LATE;
    }
    return REWARD_ABSENT;
}

bool serialize_transaction(const Transaction *tx, char *buffer, size_t buffer_size)
{
    int written = 0;
    int i;

    if (tx == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    written = snprintf(buffer, buffer_size, "reward=%d|fee=%d|inputs=%d|outputs=%d",
                       tx->is_reward ? 1 : 0, tx->fee, tx->input_count, tx->output_count);
    if (written < 0 || (size_t)written >= buffer_size) {
        return false;
    }

    for (i = 0; i < tx->input_count; i++) {
        int n = snprintf(buffer + written, buffer_size - (size_t)written, "|in:%s:%s:%d",
                         tx->inputs[i].utxo_ref, tx->inputs[i].owner, tx->inputs[i].amount);
        if (n < 0 || (size_t)(written + n) >= buffer_size) {
            return false;
        }
        written += n;
    }

    for (i = 0; i < tx->output_count; i++) {
        int n = snprintf(buffer + written, buffer_size - (size_t)written, "|out:%s:%d",
                         tx->outputs[i].owner, tx->outputs[i].amount);
        if (n < 0 || (size_t)(written + n) >= buffer_size) {
            return false;
        }
        written += n;
    }

    return true;
}

/* transaction_id stored on block = SHA-256 of serialized transaction */
bool compute_transaction_id(const Transaction *tx, char *out_id)
{
    char buffer[1024];

    if (tx == NULL || out_id == NULL) {
        return false;
    }

    if (!serialize_transaction(tx, buffer, sizeof(buffer))) {
        return false;
    }

    return sha256_hex(buffer, out_id);
}

/*
 * Attendance marking triggers a reward transaction when status is PRESENT or LATE.
 * A fixed fee is deducted before crediting the student (handled when applying to ledger).
 */
bool create_reward_transaction(const Block *block, Transaction *tx)
{
    if (block == NULL || tx == NULL) {
        return false;
    }

    memset(tx, 0, sizeof(*tx));

    if (block->token_reward <= 0) {
        return true;
    }

    tx->is_reward = true;
    tx->fee = TX_FEE;
    tx->input_count = 0;
    tx->output_count = 1;
    strncpy(tx->outputs[0].owner, block->student_id, sizeof(tx->outputs[0].owner) - 1);
    tx->outputs[0].amount = block->token_reward - TX_FEE;

    if (tx->outputs[0].amount <= 0) {
        fprintf(stderr, "ERROR: Token reward too small after fee deduction.\n");
        return false;
    }

    return compute_transaction_id(tx, tx->tx_id);
}
