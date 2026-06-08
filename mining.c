/* mining.c — proof-of-work and solo / pool / cloud mining simulations */
#include "attendance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* PoW target: hash must start with N leading '0' hex characters */
bool hash_meets_difficulty(const char *hash, int difficulty)
{
    int i;

    if (hash == NULL || difficulty < 1) {
        return false;
    }

    for (i = 0; i < difficulty; i++) {
        if (hash[i] != '0') {
            return false;
        }
    }

    return true;
}

/* Increment nonce and re-hash until difficulty target is met */
bool mine_block_pow(Block *block, int difficulty, unsigned long *attempts)
{
    unsigned long tries = 0;

    if (block == NULL || difficulty < MIN_DIFFICULTY || difficulty > MAX_DIFFICULTY) {
        return false;
    }

    block->nonce = 0;
    do {
        tries++;
        if (!compute_block_hash(block, block->hash)) {
            return false;
        }
        if (hash_meets_difficulty(block->hash, difficulty)) {
            if (attempts != NULL) {
                *attempts = tries;
            }
            return true;
        }
        block->nonce++;
    } while (block->nonce != 0);

    fprintf(stderr, "ERROR: Nonce overflow during mining.\n");
    return false;
}

/* Link block to chain tip and re-sign before PoW (index/prev_hash are final here) */
static bool prepare_block_for_mining(Block *block, const Blockchain *chain)
{
    const Block *tip = blockchain_tip(chain);

    block->index = tip == NULL ? 0 : tip->index + 1;
    strncpy(block->previous_hash, tip == NULL ? GENESIS_PREV_HASH : tip->hash,
            sizeof(block->previous_hash) - 1);
    block->nonce = 0;

    return sign_block(block);
}

/* Full confirm flow: mine -> append -> credit student -> credit miner */
static bool confirm_pending_block(Blockchain *chain, PendingPool *pool, Ledger *ledger,
                                  int difficulty, const char *miner_id,
                                  unsigned long *attempts_out)
{
    Block block;
    Transaction tx;
    unsigned long attempts = 0;

    if (!pending_remove_front(pool, &block)) {
        return false;
    }

    if (!prepare_block_for_mining(&block, chain)) {
        fprintf(stderr, "ERROR: Failed to prepare block for mining.\n");
        return false;
    }

    if (!mine_block_pow(&block, difficulty, &attempts)) {
        fprintf(stderr, "ERROR: Proof-of-work failed for pending block.\n");
        return false;
    }

    if (!blockchain_append_mined_block(chain, &block)) {
        return false;
    }

    memset(&tx, 0, sizeof(tx));
    if (block.token_reward > 0) {
        if (!create_reward_transaction(&block, &tx)) {
            return false;
        }
        if (!ledger_apply_confirmed_block(ledger, &block, &tx)) {
            return false;
        }
    }

    if (miner_id != NULL && MINING_REWARD > 0) {
        if (!ledger_credit_miner(ledger, miner_id, MINING_REWARD)) {
            return false;
        }
    }

    if (attempts_out != NULL) {
        *attempts_out += attempts;
    }

    printf("Confirmed block #%d for %s (%s) | hash attempts: %lu\n", block.index,
           block.full_name, block.status, attempts);

    if (ledger->model == LEDGER_UTXO) {
        ledger_print_utxo_set(ledger);
    }

    return true;
}

bool mining_solo(Blockchain *chain, PendingPool *pool, Ledger *ledger, int difficulty,
                 const char *miner_id)
{
    unsigned long total_attempts = 0;
    size_t confirmed = 0;
    const char *miner = miner_id != NULL ? miner_id : "SOLO_MINER";

    if (pending_is_empty(pool)) {
        printf("No pending blocks to mine.\n");
        return false;
    }

    printf("\n=== Solo Mining (difficulty %d) ===\n", difficulty);
    while (!pending_is_empty(pool)) {
        if (!confirm_pending_block(chain, pool, ledger, difficulty, miner, &total_attempts)) {
            return false;
        }
        confirmed++;
    }

    printf("Solo mining complete: %zu block(s) confirmed.\n", confirmed);
    printf("Total hash attempts: %lu\n", total_attempts);
    printf("Miner %s received %d coin(s) per block (%zu total mining reward).\n", miner,
           MINING_REWARD, confirmed);
    return true;
}

bool mining_pool(Blockchain *chain, PendingPool *pool, Ledger *ledger, int difficulty,
                 int miner_count)
{
    int miners = miner_count > 0 ? miner_count : 4;
    int attempts[MAX_POOL_MINERS];
    int total_attempts = 0;
    int gross_reward;
    int pool_fee;
    int distributable;
    int i;
    size_t blocks_to_mine;
    size_t b;

    if (miners > MAX_POOL_MINERS) {
        miners = MAX_POOL_MINERS;
    }

    if (pending_is_empty(pool)) {
        printf("No pending blocks to mine.\n");
        return false;
    }

    blocks_to_mine = pending_count(pool);
    gross_reward = (int)blocks_to_mine * MINING_REWARD;
    pool_fee = (gross_reward * POOL_FEE_PERCENT) / 100;
    distributable = gross_reward - pool_fee;

    srand((unsigned int)time(NULL));
    memset(attempts, 0, sizeof(attempts));

    /* Simulate random hash rates; share = (miner_attempts / total) * reward */
    for (i = 0; i < miners; i++) {
        attempts[i] = 50 + (rand() % 151);
        total_attempts += attempts[i];
    }

    printf("\n=== Pool Mining (difficulty %d, %d miners) ===\n", difficulty, miners);
    printf("%-10s %-10s %-12s %-10s\n", "Miner ID", "Attempts", "Share %", "Reward");
    printf("------------------------------------------------------\n");

    for (i = 0; i < miners; i++) {
        double share = total_attempts == 0 ? 0.0 : (100.0 * attempts[i]) / total_attempts;
        int reward = total_attempts == 0 ? 0 : (attempts[i] * distributable) / total_attempts;
        char miner_id[16];

        snprintf(miner_id, sizeof(miner_id), "POOL%03d", i + 1);
        printf("%-10s %-10d %-11.2f%% %-10d\n", miner_id, attempts[i], share, reward);
        if (reward > 0) {
            ledger_credit_miner(ledger, miner_id, reward);
        }
    }

    printf("Pool fee (%d%%): %d coin(s)\n", POOL_FEE_PERCENT, pool_fee);
    printf("------------------------------------------------------\n");

    for (b = 0; b < blocks_to_mine; b++) {
        Block block;
        Transaction tx;
        unsigned long pow_attempts = 0;

        if (!pending_remove_front(pool, &block)) {
            return false;
        }

        if (!prepare_block_for_mining(&block, chain)) {
            return false;
        }

        if (!mine_block_pow(&block, difficulty, &pow_attempts)) {
            return false;
        }

        if (!blockchain_append_mined_block(chain, &block)) {
            return false;
        }

        memset(&tx, 0, sizeof(tx));
        if (block.token_reward > 0) {
            create_reward_transaction(&block, &tx);
            ledger_apply_confirmed_block(ledger, &block, &tx);
        }

        printf("Pool confirmed block #%d for %s (PoW attempts: %lu)\n", block.index,
               block.student_id, pow_attempts);

        if (ledger->model == LEDGER_UTXO) {
            ledger_print_utxo_set(ledger);
        }
    }

    printf("============================================\n\n");
    return true;
}

bool mining_cloud(Blockchain *chain, PendingPool *pool, Ledger *ledger, int difficulty, int rounds,
                  int rental_fee, int maintenance_fee)
{
    int r;
    int gross_earnings = 0;
    int total_fees = 0;
    int net_profit = 0;
    int cumulative_gross = 0;
    int cumulative_fees = 0;
    size_t blocks_to_mine;

    if (rounds < CLOUD_MIN_ROUNDS || rounds > CLOUD_MAX_ROUNDS) {
        fprintf(stderr, "ERROR: Cloud rental rounds must be between %d and %d.\n", CLOUD_MIN_ROUNDS,
                CLOUD_MAX_ROUNDS);
        return false;
    }

    if (pending_is_empty(pool)) {
        printf("No pending blocks to mine.\n");
        return false;
    }

    blocks_to_mine = pending_count(pool);

    printf("\n=== Cloud Mining (%d round(s)) ===\n", rounds);
    printf("%-6s %-10s %-12s %-12s %-10s\n", "Round", "Rental", "Maintenance", "Reward", "Net");
    printf("------------------------------------------------------------\n");

    /* Simulate rental rounds; warn when cumulative fees exceed rewards */
    for (r = 1; r <= rounds; r++) {
        int round_reward = MINING_REWARD * (int)blocks_to_mine;
        int round_fees = rental_fee + maintenance_fee;
        int round_net = round_reward - round_fees;

        gross_earnings += round_reward;
        total_fees += round_fees;
        net_profit += round_net;
        cumulative_gross += round_reward;
        cumulative_fees += round_fees;

        printf("%-6d %-10d %-12d %-12d %-10d\n", r, rental_fee, maintenance_fee, round_reward,
               round_net);

        if (cumulative_fees > cumulative_gross) {
            printf("  WARNING: Rental is unprofitable (fees exceed rewards).\n");
        }
    }

    printf("------------------------------------------------------------\n");
    printf("Gross earnings : %d coin(s)\n", gross_earnings);
    printf("Total fees paid: %d coin(s)\n", total_fees);
    printf("Net profit     : %d coin(s)\n", net_profit);

    if (total_fees > gross_earnings) {
        printf("WARNING: Overall cloud rental is unprofitable.\n");
    }

    while (!pending_is_empty(pool)) {
        Block block;
        Transaction tx;
        unsigned long pow_attempts = 0;

        if (!pending_remove_front(pool, &block)) {
            return false;
        }

        if (!prepare_block_for_mining(&block, chain)) {
            return false;
        }

        if (!mine_block_pow(&block, difficulty, &pow_attempts)) {
            return false;
        }

        if (!blockchain_append_mined_block(chain, &block)) {
            return false;
        }

        memset(&tx, 0, sizeof(tx));
        if (block.token_reward > 0) {
            create_reward_transaction(&block, &tx);
            ledger_apply_confirmed_block(ledger, &block, &tx);
        }

        printf("Cloud confirmed block #%d for %s (PoW attempts: %lu)\n", block.index,
               block.student_id, pow_attempts);
    }

    if (net_profit > 0) {
        ledger_credit_miner(ledger, "CLOUD_USER", net_profit);
    }

    if (ledger->model == LEDGER_UTXO) {
        ledger_print_utxo_set(ledger);
    }

    printf("================================\n\n");
    return true;
}
