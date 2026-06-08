/*
 * Blockchain Attendance System
 *
 * Attendance marking creates token reward transactions (10 PRESENT, 5 LATE, 0 ABSENT)
 * that enter a pending pool. Mining confirms blocks and updates the chosen ledger model.
 */
#include "attendance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LedgerModel g_model = LEDGER_UTXO;       /* active ledger: UTXO or account */
static int g_difficulty = DEFAULT_DIFFICULTY;   /* PoW leading-zero target */

static void print_help(void)
{
    printf("\nCommands:\n");
    printf("  mark <id> <PRESENT|ABSENT|LATE>     Queue attendance (creates token tx if applicable)\n");
    printf("  pending                             Show unconfirmed blocks\n");
    printf("  mine solo                           Solo PoW mining\n");
    printf("  mine pool [miners]                  Pool mining simulation\n");
    printf("  mine cloud <rounds> <rental> <maint> Cloud mining simulation (1-5 rounds)\n");
    printf("  view                                Show confirmed records\n");
    printf("  validate                            Verify blockchain integrity\n");
    printf("  balance <student_id>                Show student token balance\n");
    printf("  balances                            Show all student balances\n");
    printf("  utxos                               Print full UTXO set (UTXO model)\n");
    printf("  transfer <from> <to> <amount>       Manual token transfer\n");
    printf("  transfer_nonce <from> <to> <amt> <n> Transfer with explicit nonce (account)\n");
    printf("  history <student_id>                Account tx history (account model)\n");
    printf("  model <utxo|account>                Switch ledger model (resets ledger)\n");
    printf("  difficulty <1-4>                    Set PoW difficulty\n");
    printf("  tamper <block_index>                Demo tamper detection\n");
    printf("  help                                Show this help\n");
    printf("  quit                                Exit\n\n");
}

static void trim_input(char *input)
{
    size_t len = strlen(input);

    while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r' || input[len - 1] == ' ')) {
        input[len - 1] = '\0';
        len--;
    }
}

static LedgerModel parse_model_arg(const char *arg)
{
    if (arg == NULL) {
        return g_model;
    }
    if (strcmp(arg, "utxo") == 0 || strcmp(arg, "UTXO") == 0) {
        return LEDGER_UTXO;
    }
    if (strcmp(arg, "account") == 0 || strcmp(arg, "ACCOUNT") == 0) {
        return LEDGER_ACCOUNT;
    }
    return g_model;
}

static void parse_cli_args(int argc, char **argv, StudentRegistry *registry)
{
    int i;

    (void)registry;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            g_model = parse_model_arg(argv[++i]);
        } else if (strcmp(argv[i], "--difficulty") == 0 && i + 1 < argc) {
            g_difficulty = atoi(argv[++i]);
            if (g_difficulty < MIN_DIFFICULTY) {
                g_difficulty = MIN_DIFFICULTY;
            }
            if (g_difficulty > MAX_DIFFICULTY) {
                g_difficulty = MAX_DIFFICULTY;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--model utxo|account] [--difficulty 1-4]\n", argv[0]);
            exit(0);
        }
    }
}

int main(int argc, char **argv)
{
    StudentRegistry registry;
    Blockchain chain;
    PendingPool pool;
    Ledger ledger;
    char line[256];
    char command[32];
    char arg1[STUDENT_ID_LEN];
    char arg2[STUDENT_ID_LEN];
    char arg3[32];
    char arg4[32];
    int tamper_index;
    int pool_miners;
    int cloud_rounds;
    int rental_fee;
    int maintenance_fee;
    int amount;
    int nonce;

    printf("Blockchain Attendance & Mining System \n");
    printf("====================================================\n");

    parse_cli_args(argc, argv, &registry);

    if (!load_student_registry(&registry, STUDENTS_FILE)) {
        return 1;
    }

    if (!crypto_init_keys()) {
        crypto_cleanup();
        return 1;
    }

    pending_init(&pool);

    /* Load saved chain or create genesis block */
    if (!blockchain_load(&chain, CHAIN_FILE)) {
        if (!blockchain_init(&chain)) {
            crypto_cleanup();
            return 1;
        }
        if (!blockchain_save(&chain, CHAIN_FILE)) {
            crypto_cleanup();
            return 1;
        }
    }

    if (!ledger_init(&ledger, g_model, &registry)) {
        crypto_cleanup();
        return 1;
    }

    printf("Ledger model : %s\n", g_model == LEDGER_UTXO ? "UTXO" : "Account");
    printf("PoW difficulty: %d leading zero(s)\n", g_difficulty);
    print_help();

    /* Interactive command loop */
    while (1) {
        printf("attendance> ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        trim_input(line);
        if (line[0] == '\0') {
            continue;
        }

        if (sscanf(line, "%31s", command) != 1) {
            continue;
        }

        if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            break;
        } else if (strcmp(command, "help") == 0) {
            print_help();
        } else if (strcmp(command, "view") == 0) {
            blockchain_print_records(&chain);
        } else if (strcmp(command, "pending") == 0) {
            pending_print(&pool);
        } else if (strcmp(command, "validate") == 0) {
            if (!blockchain_validate(&chain, g_difficulty)) {
                printf("Chain validation FAILED.\n");
            }
        } else if (strcmp(command, "balances") == 0) {
            ledger_print_balances(&ledger, &registry);
        } else if (strcmp(command, "utxos") == 0) {
            ledger_print_utxo_set(&ledger);
        } else if (strcmp(command, "mark") == 0) {
            if (sscanf(line, "%31s %19s %9s", command, arg1, arg2) != 3) {
                printf("Usage: mark <student_id> <PRESENT|ABSENT|LATE>\n");
                continue;
            }
            mark_attendance(&pool, &chain, &registry, arg1, arg2);
        } else if (strcmp(command, "balance") == 0) {
            if (sscanf(line, "%31s %19s", command, arg1) != 2) {
                printf("Usage: balance <student_id>\n");
                continue;
            }
            printf("Balance for %s: %d coin(s)\n", arg1, ledger_balance(&ledger, arg1));
        } else if (strcmp(command, "transfer") == 0) {
            if (sscanf(line, "%31s %19s %19s %31s", command, arg1, arg2, arg3) != 4) {
                printf("Usage: transfer <from> <to> <amount>\n");
                continue;
            }
            amount = atoi(arg3);
            if (find_student(&registry, arg1) == NULL || find_student(&registry, arg2) == NULL) {
                printf("ERROR: Sender or recipient not in student registry.\n");
                continue;
            }
            ledger_transfer(&ledger, arg1, arg2, amount);
        } else if (strcmp(command, "transfer_nonce") == 0) {
            if (sscanf(line, "%31s %19s %19s %31s %31s", command, arg1, arg2, arg3, arg4) != 5) {
                printf("Usage: transfer_nonce <from> <to> <amount> <nonce>\n");
                continue;
            }
            amount = atoi(arg3);
            nonce = atoi(arg4);
            ledger_transfer_with_nonce(&ledger, arg1, arg2, amount, nonce);
        } else if (strcmp(command, "history") == 0) {
            if (sscanf(line, "%31s %19s", command, arg1) != 2) {
                printf("Usage: history <student_id>\n");
                continue;
            }
            ledger_print_history(&ledger, arg1);
        } else if (strcmp(command, "model") == 0) {
            if (sscanf(line, "%31s %31s", command, arg1) != 2) {
                printf("Usage: model <utxo|account>\n");
                continue;
            }
            g_model = parse_model_arg(arg1);
            ledger_set_model(&ledger, g_model, &registry);
            printf("Switched to %s model (ledger reset).\n",
                   g_model == LEDGER_UTXO ? "UTXO" : "Account");
        } else if (strcmp(command, "difficulty") == 0) {
            if (sscanf(line, "%31s %31s", command, arg1) != 2) {
                printf("Usage: difficulty <1-4>\n");
                continue;
            }
            g_difficulty = atoi(arg1);
            if (g_difficulty < MIN_DIFFICULTY || g_difficulty > MAX_DIFFICULTY) {
                printf("Difficulty must be between %d and %d.\n", MIN_DIFFICULTY, MAX_DIFFICULTY);
                continue;
            }
            printf("PoW difficulty set to %d leading zero(s).\n", g_difficulty);
        } else if (strcmp(command, "mine") == 0) {
            if (sscanf(line, "%31s %31s", command, arg1) != 2) {
                printf("Usage: mine solo | mine pool [miners] | mine cloud <rounds> <rental> <maint>\n");
                continue;
            }
            if (strcmp(arg1, "solo") == 0) {
                mining_solo(&chain, &pool, &ledger, g_difficulty, "SOLO_MINER");
            } else if (strcmp(arg1, "pool") == 0) {
                pool_miners = 4;
                if (sscanf(line, "%31s %31s %31s", command, arg1, arg2) == 3) {
                    pool_miners = atoi(arg2);
                }
                mining_pool(&chain, &pool, &ledger, g_difficulty, pool_miners);
            } else if (strcmp(arg1, "cloud") == 0) {
                if (sscanf(line, "%31s %31s %31s %31s %31s", command, arg1, arg2, arg3, arg4) != 5) {
                    printf("Usage: mine cloud <rounds 1-5> <rental_fee> <maintenance_fee>\n");
                    continue;
                }
                cloud_rounds = atoi(arg2);
                rental_fee = atoi(arg3);
                maintenance_fee = atoi(arg4);
                mining_cloud(&chain, &pool, &ledger, g_difficulty, cloud_rounds, rental_fee,
                             maintenance_fee);
            } else {
                printf("Unknown mining mode: %s\n", arg1);
            }
        } else if (strcmp(command, "tamper") == 0) {
            if (sscanf(line, "%31s %d", command, &tamper_index) != 2) {
                printf("Usage: tamper <block_index>\n");
                continue;
            }
            blockchain_tamper_demo(&chain, tamper_index);
        } else {
            printf("Unknown command: %s (type 'help' for options)\n", command);
        }
    }

    ledger_cleanup(&ledger);
    crypto_cleanup();
    printf("Goodbye.\n");
    return 0;
}
