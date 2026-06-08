/* pending.c — unconfirmed blocks and attendance marking flow */
#include "attendance.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void pending_init(PendingPool *pool)
{
    if (pool != NULL) {
        memset(pool, 0, sizeof(*pool));
    }
}

bool pending_add(PendingPool *pool, const Block *block)
{
    if (pool == NULL || block == NULL) {
        return false;
    }

    if (pool->count >= MAX_PENDING) {
        fprintf(stderr, "ERROR: Pending pool is full.\n");
        return false;
    }

    pool->blocks[pool->count++] = *block;
    return true;
}

bool pending_is_empty(const PendingPool *pool)
{
    return pool == NULL || pool->count == 0;
}

size_t pending_count(const PendingPool *pool)
{
    return pool == NULL ? 0 : pool->count;
}

bool pending_get(const PendingPool *pool, size_t index, Block *out)
{
    if (pool == NULL || out == NULL || index >= pool->count) {
        return false;
    }

    *out = pool->blocks[index];
    return true;
}

void pending_clear(PendingPool *pool)
{
    if (pool != NULL) {
        pool->count = 0;
    }
}

bool pending_remove_front(PendingPool *pool, Block *out)
{
    size_t i;

    if (pool == NULL || pool->count == 0) {
        return false;
    }

    if (out != NULL) {
        *out = pool->blocks[0];
    }

    for (i = 1; i < pool->count; i++) {
        pool->blocks[i - 1] = pool->blocks[i];
    }
    pool->count--;
    return true;
}

void pending_print(const PendingPool *pool)
{
    size_t i;

    printf("\n=== Pending Pool (%zu unconfirmed block(s)) ===\n",
           pool == NULL ? 0 : pool->count);

    if (pool == NULL || pool->count == 0) {
        printf("No blocks waiting for mining.\n");
        return;
    }

    for (i = 0; i < pool->count; i++) {
        const Block *block = &pool->blocks[i];
        char time_buffer[32];

        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S",
                 localtime(&block->timestamp));

        printf("\nPending #%zu\n", i);
        printf("  Student ID     : %s\n", block->student_id);
        printf("  Name           : %s\n", block->full_name);
        printf("  Course         : %s\n", block->course_code);
        printf("  Status         : %s\n", block->status);
        printf("  Timestamp      : %s\n", time_buffer);
        printf("  Token Reward   : %d\n", block->token_reward);
        printf("  Transaction ID : %s\n",
               block->transaction_id[0] ? block->transaction_id : "(none)");
    }

    printf("\n=============================================\n\n");
}

static void init_pending_block_fields(Block *block, int index, const char *previous_hash)
{
    memset(block, 0, sizeof(*block));
    block->index = index;
    block->timestamp = time(NULL);
    block->nonce = 0;
    strncpy(block->previous_hash, previous_hash, sizeof(block->previous_hash) - 1);
}

/* Sign block and attach transaction_id; real hash is found during mining */
static bool finalize_pending_block(Block *block, Transaction *tx)
{
    if (block->token_reward > 0) {
        if (!create_reward_transaction(block, tx)) {
            return false;
        }
        strncpy(block->transaction_id, tx->tx_id, sizeof(block->transaction_id) - 1);
    } else {
        block->transaction_id[0] = '\0';
    }

    if (!sign_block(block)) {
        fprintf(stderr, "ERROR: Failed to sign pending block.\n");
        return false;
    }

  /* Hash computed during mining (PoW); placeholder until mined */
    memset(block->hash, '0', 64);
    block->hash[64] = '\0';
    return true;
}

/*
 * Mark attendance: creates block + optional reward tx, queues to pending pool.
 * Balances are NOT updated until mining confirms the block.
 */
bool mark_attendance(PendingPool *pool, const Blockchain *chain, const StudentRegistry *registry,
                     const char *student_id, const char *status)
{
    const Student *student;
    Block block;
    Transaction tx;
    const Block *tip;
    int next_index;

    if (pool == NULL || chain == NULL || registry == NULL || student_id == NULL || status == NULL) {
        return false;
    }

    if (!is_valid_status(status)) {
        fprintf(stderr, "ERROR: Status must be PRESENT, ABSENT, or LATE.\n");
        return false;
    }

    student = find_student(registry, student_id);
    if (student == NULL) {
        fprintf(stderr, "ERROR: Student ID not found\n");
        return false;
    }

    if (pool->count >= MAX_PENDING) {
        fprintf(stderr, "ERROR: Pending pool is full.\n");
        return false;
    }

    /* Tentative index; final index set from chain tip at mining time */
    tip = blockchain_tip(chain);
    next_index = tip == NULL ? 0 : tip->index + 1 + (int)pool->count;

    init_pending_block_fields(&block, next_index, tip == NULL ? GENESIS_PREV_HASH : tip->hash);
    strncpy(block.student_id, student->student_id, sizeof(block.student_id) - 1);
    strncpy(block.full_name, student->full_name, sizeof(block.full_name) - 1);
    strncpy(block.course_code, student->course_code, sizeof(block.course_code) - 1);
    strncpy(block.status, status, sizeof(block.status) - 1);
    block.token_reward = token_reward_for_status(status);

    if (!finalize_pending_block(&block, &tx)) {
        return false;
    }

    if (!pending_add(pool, &block)) {
        return false;
    }

    printf("Attendance queued for %s (%s) as %s.\n", student->full_name, student->student_id,
           status);
    printf("  Token reward   : %d coin(s)\n", block.token_reward);
    if (block.token_reward > 0) {
        printf("  Transaction ID : %s\n", block.transaction_id);
        printf("  Net credit after fee: %d coin(s) (fee: %d)\n",
               block.token_reward - TX_FEE, TX_FEE);
    } else {
        printf("  No transaction created (ABSENT).\n");
    }
    printf("  Status: PENDING — mine to confirm on chain.\n");
    return true;
}
