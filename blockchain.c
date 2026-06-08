/* blockchain.c — confirmed chain persistence, validation, and tamper demo */
#include "attendance.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void init_block_fields(Block *block, int index, const char *previous_hash)
{
    memset(block, 0, sizeof(*block));
    block->index = index;
    block->timestamp = time(NULL);
    block->nonce = 0;
    block->token_reward = 0;
    block->transaction_id[0] = '\0';
    strncpy(block->previous_hash, previous_hash, sizeof(block->previous_hash) - 1);
}

static bool finalize_genesis_block(Block *block)
{
    if (!sign_block(block)) {
        fprintf(stderr, "ERROR: Failed to sign genesis block.\n");
        return false;
    }

    if (!compute_block_hash(block, block->hash)) {
        fprintf(stderr, "ERROR: Failed to compute genesis hash.\n");
        return false;
    }

    return true;
}

static bool create_genesis_block(Block *block)
{
    init_block_fields(block, 0, GENESIS_PREV_HASH);
    strncpy(block->student_id, "GENESIS", sizeof(block->student_id) - 1);
    strncpy(block->full_name, "Genesis Block", sizeof(block->full_name) - 1);
    strncpy(block->course_code, "SYSTEM", sizeof(block->course_code) - 1);
    strncpy(block->status, "PRESENT", sizeof(block->status) - 1);
    return finalize_genesis_block(block);
}

bool blockchain_init(Blockchain *chain)
{
    if (chain == NULL) {
        return false;
    }

    memset(chain, 0, sizeof(*chain));
    if (!create_genesis_block(&chain->blocks[0])) {
        return false;
    }

    chain->length = 1;
    printf("Genesis block created (index 0).\n");
    return true;
}

bool blockchain_load(Blockchain *chain, const char *path)
{
    FILE *file;
    size_t count;

    if (chain == NULL || path == NULL) {
        return false;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    if (fread(&count, sizeof(count), 1, file) != 1 || count == 0 || count > MAX_BLOCKS) {
        fclose(file);
        return false;
    }

    if (fread(chain->blocks, sizeof(Block), count, file) != count) {
        fclose(file);
        return false;
    }

    fclose(file);
    chain->length = count;
    printf("Loaded blockchain with %zu block(s) from %s.\n", chain->length, path);
    return true;
}

bool blockchain_save(const Blockchain *chain, const char *path)
{
    FILE *file;
    size_t count;

    if (chain == NULL || path == NULL || chain->length == 0) {
        return false;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "ERROR: Unable to save blockchain to %s.\n", path);
        return false;
    }

    count = chain->length;
    if (fwrite(&count, sizeof(count), 1, file) != 1 ||
        fwrite(chain->blocks, sizeof(Block), count, file) != count) {
        fclose(file);
        fprintf(stderr, "ERROR: Failed while writing blockchain data.\n");
        return false;
    }

    fclose(file);
    return true;
}

const Block *blockchain_tip(const Blockchain *chain)
{
    if (chain == NULL || chain->length == 0) {
        return NULL;
    }

    return &chain->blocks[chain->length - 1];
}

/* Append a PoW-validated block (already linked and hashed by mining) */
bool blockchain_append_mined_block(Blockchain *chain, const Block *block)
{
    if (chain == NULL || block == NULL) {
        return false;
    }

    if (chain->length >= MAX_BLOCKS) {
        fprintf(stderr, "ERROR: Blockchain has reached maximum capacity.\n");
        return false;
    }

    chain->blocks[chain->length++] = *block;

    if (!blockchain_save(chain, CHAIN_FILE)) {
        chain->length--;
        return false;
    }

    return true;
}

bool blockchain_validate(const Blockchain *chain, int difficulty)
{
    size_t i;
    char expected_hash[HASH_LEN];

    if (chain == NULL || chain->length == 0) {
        fprintf(stderr, "ERROR: Blockchain is empty.\n");
        return false;
    }

    for (i = 0; i < chain->length; i++) {
        const Block *block = &chain->blocks[i];
        const Block *previous = (i > 0) ? &chain->blocks[i - 1] : NULL;

        if (!compute_block_hash(block, expected_hash)) {
            fprintf(stderr, "Block %d: hash computation failed.\n", block->index);
            return false;
        }

        if (strcmp(expected_hash, block->hash) != 0) {
            fprintf(stderr, "Block %d: hash mismatch (possible tampering).\n", block->index);
            return false;
        }

        /* Genesis block is exempt from PoW difficulty check */
        if (i > 0 && !hash_meets_difficulty(block->hash, difficulty)) {
            fprintf(stderr, "Block %d: hash does not meet difficulty %d.\n", block->index,
                    difficulty);
            return false;
        }

        if (i == 0) {
            if (strcmp(block->previous_hash, GENESIS_PREV_HASH) != 0) {
                fprintf(stderr, "Genesis block: invalid previous_hash.\n");
                return false;
            }
        } else if (strcmp(block->previous_hash, previous->hash) != 0) {
            fprintf(stderr, "Block %d: previous_hash does not link to block %d.\n", block->index,
                    previous->index);
            return false;
        }

        if (!verify_block_signature(block)) {
            fprintf(stderr, "Block %d: invalid digital signature.\n", block->index);
            return false;
        }
    }

    printf("Chain validation PASSED (%zu block(s) verified).\n", chain->length);
    return true;
}

void blockchain_print_records(const Blockchain *chain)
{
    size_t i;

    if (chain == NULL || chain->length == 0) {
        printf("No attendance records found.\n");
        return;
    }

    printf("\n=== Confirmed Attendance Records ===\n");
    for (i = 0; i < chain->length; i++) {
        const Block *block = &chain->blocks[i];
        char time_buffer[32];
        bool signature_ok = verify_block_signature(block);

        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S",
                 localtime(&block->timestamp));

        printf("\nBlock #%d\n", block->index);
        printf("  Student ID     : %s\n", block->student_id);
        printf("  Name           : %s\n", block->full_name);
        printf("  Course         : %s\n", block->course_code);
        printf("  Status         : %s\n", block->status);
        printf("  Timestamp      : %s\n", time_buffer);
        printf("  Token Reward   : %d\n", block->token_reward);
        printf("  Transaction ID : %s\n",
               block->transaction_id[0] ? block->transaction_id : "(none)");
        printf("  Nonce          : %lu\n", block->nonce);
        printf("  Hash           : %s\n", block->hash);
        printf("  Prev Hash      : %s\n", block->previous_hash);
        printf("  Signature      : %s\n", signature_ok ? "VALID" : "INVALID");
    }
    printf("\n====================================\n");
}

/* Deliberately corrupt a block without re-hashing to demo integrity checks */
bool blockchain_tamper_demo(Blockchain *chain, int block_index)
{
    Block *block;

    if (chain == NULL || block_index < 0 || (size_t)block_index >= chain->length) {
        fprintf(stderr, "ERROR: Invalid block index for tamper demo.\n");
        return false;
    }

    if (block_index == 0) {
        fprintf(stderr, "ERROR: Cannot tamper with genesis block in demo.\n");
        return false;
    }

    block = &chain->blocks[block_index];
    if (strcmp(block->status, "ABSENT") == 0) {
        printf("Tampering block #%d: changing status from ABSENT to PRESENT (without re-hashing).\n",
               block->index);
        strncpy(block->status, "PRESENT", sizeof(block->status) - 1);
    } else {
        printf("Tampering block #%d: changing status from %s to ABSENT (without re-hashing).\n",
               block->index, block->status);
        strncpy(block->status, "ABSENT", sizeof(block->status) - 1);
    }

    if (!blockchain_save(chain, CHAIN_FILE)) {
        return false;
    }

    printf("Tamper applied. Run 'validate' to observe chain failure.\n");
    return true;
}
