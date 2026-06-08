/* crypto.c — SHA-256 hashing and ECDSA (P-256) signing for blocks */
#include "attendance.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

#include <stdio.h>
#include <string.h>

static EVP_PKEY *private_key = NULL;
static EVP_PKEY *public_key = NULL;

static bool write_pem_key(const char *path, EVP_PKEY *key, bool is_private)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        fprintf(stderr, "ERROR: Unable to write key file %s.\n", path);
        return false;
    }

    if (is_private) {
        if (!PEM_write_PrivateKey(file, key, NULL, NULL, 0, NULL, NULL)) {
            fclose(file);
            fprintf(stderr, "ERROR: Failed to write private key.\n");
            return false;
        }
    } else if (!PEM_write_PUBKEY(file, key)) {
        fclose(file);
        fprintf(stderr, "ERROR: Failed to write public key.\n");
        return false;
    }

    fclose(file);
    return true;
}

static bool load_or_create_keys(void)
{
    FILE *private_file = fopen(PRIVATE_KEY_FILE, "rb");
    FILE *public_file = fopen(PUBLIC_KEY_FILE, "rb");

    if (private_file != NULL && public_file != NULL) {
        private_key = PEM_read_PrivateKey(private_file, NULL, NULL, NULL);
        public_key = PEM_read_PUBKEY(public_file, NULL, NULL, NULL);
        fclose(private_file);
        fclose(public_file);

        if (private_key == NULL || public_key == NULL) {
            fprintf(stderr, "ERROR: Failed to load existing key files.\n");
            return false;
        }

        printf("Loaded ECDSA key pair from disk.\n");
        return true;
    }

    if (private_file != NULL) {
        fclose(private_file);
    }
    if (public_file != NULL) {
        fclose(public_file);
    }

    private_key = EVP_EC_gen("P-256");
    if (private_key == NULL) {
        fprintf(stderr, "ERROR: Failed to generate ECDSA key pair.\n");
        return false;
    }

    public_key = EVP_PKEY_dup(private_key);
    if (public_key == NULL) {
        fprintf(stderr, "ERROR: Failed to duplicate public key.\n");
        return false;
    }

    if (!write_pem_key(PRIVATE_KEY_FILE, private_key, true) ||
        !write_pem_key(PUBLIC_KEY_FILE, public_key, false)) {
        return false;
    }

    printf("Generated new ECDSA key pair (P-256).\n");
    return true;
}

bool crypto_init_keys(void)
{
    return load_or_create_keys();
}

void crypto_cleanup(void)
{
    if (private_key != NULL) {
        EVP_PKEY_free(private_key);
        private_key = NULL;
    }
    if (public_key != NULL) {
        EVP_PKEY_free(public_key);
        public_key = NULL;
    }
}

/* Hash includes nonce (PoW); signature excludes it so nonce can change during mining */
static int serialize_block_payload(const Block *block, unsigned char *buffer, size_t buffer_size,
                                   bool include_nonce)
{
    if (include_nonce) {
        return snprintf((char *)buffer, buffer_size,
                        "%d|%lld|%s|%s|%s|%s|%d|%s|%s|%lu",
                        block->index, (long long)block->timestamp, block->student_id,
                        block->full_name, block->course_code, block->status, block->token_reward,
                        block->transaction_id, block->previous_hash, block->nonce);
    }

    return snprintf((char *)buffer, buffer_size,
                    "%d|%lld|%s|%s|%s|%s|%d|%s|%s",
                    block->index, (long long)block->timestamp, block->student_id,
                    block->full_name, block->course_code, block->status, block->token_reward,
                    block->transaction_id, block->previous_hash);
}

static int serialize_block_for_hash(const Block *block, unsigned char *buffer, size_t buffer_size)
{
    return serialize_block_payload(block, buffer, buffer_size, true);
}

static int serialize_block_for_signature(const Block *block, unsigned char *buffer, size_t buffer_size)
{
    return serialize_block_payload(block, buffer, buffer_size, false);
}

bool sha256_hex(const char *payload, char *out_hash)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned int i;

    if (payload == NULL || out_hash == NULL) {
        return false;
    }

    if (!SHA256((const unsigned char *)payload, strlen(payload), digest)) {
        return false;
    }

    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(out_hash + (i * 2), "%02x", digest[i]);
    }
    out_hash[64] = '\0';
    return true;
}

bool compute_block_hash(const Block *block, char *out_hash)
{
    unsigned char payload[768];
    int payload_len;

    if (block == NULL || out_hash == NULL) {
        return false;
    }

    payload_len = serialize_block_for_hash(block, payload, sizeof(payload));
    if (payload_len < 0 || (size_t)payload_len >= sizeof(payload)) {
        return false;
    }

    return sha256_hex((char *)payload, out_hash);
}

bool sign_block(Block *block)
{
    unsigned char payload[768];
    int payload_len;
    EVP_MD_CTX *context = NULL;
    size_t signature_len = 0;
    unsigned char signature_buffer[SIGNATURE_LEN];

    if (block == NULL || private_key == NULL) {
        return false;
    }

    memset(block->signature, 0, sizeof(block->signature));

    payload_len = serialize_block_for_signature(block, payload, sizeof(payload));
    if (payload_len < 0 || (size_t)payload_len >= sizeof(payload)) {
        return false;
    }

    context = EVP_MD_CTX_new();
    if (context == NULL) {
        return false;
    }

    signature_len = sizeof(signature_buffer);
    if (EVP_DigestSignInit(context, NULL, EVP_sha256(), NULL, private_key) != 1 ||
        EVP_DigestSign(context, signature_buffer, &signature_len, payload, (size_t)payload_len) <= 0) {
        EVP_MD_CTX_free(context);
        return false;
    }

    EVP_MD_CTX_free(context);

    if (signature_len > SIGNATURE_LEN) {
        fprintf(stderr, "ERROR: Signature length exceeds buffer.\n");
        return false;
    }

    memcpy(block->signature, signature_buffer, signature_len);
    return true;
}

size_t signature_length(const unsigned char *signature, size_t max_len)
{
    size_t i = max_len;

    while (i > 0 && signature[i - 1] == 0) {
        i--;
    }

    return i;
}

bool verify_block_signature(const Block *block)
{
    unsigned char payload[768];
    int payload_len;
    EVP_MD_CTX *context = NULL;
    size_t sig_len;

    if (block == NULL || public_key == NULL) {
        return false;
    }

    sig_len = signature_length(block->signature, SIGNATURE_LEN);
    if (sig_len == 0) {
        return false;
    }

    payload_len = serialize_block_for_signature(block, payload, sizeof(payload));
    if (payload_len < 0 || (size_t)payload_len >= sizeof(payload)) {
        return false;
    }

    context = EVP_MD_CTX_new();
    if (context == NULL) {
        return false;
    }

    if (EVP_DigestVerifyInit(context, NULL, EVP_sha256(), NULL, public_key) != 1 ||
        EVP_DigestVerify(context, block->signature, sig_len, payload, (size_t)payload_len) != 1) {
        EVP_MD_CTX_free(context);
        return false;
    }

    EVP_MD_CTX_free(context);
    return true;
}
