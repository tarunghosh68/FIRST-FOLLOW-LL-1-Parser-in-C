#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NT 20
#define MAX_T 30
#define MAX_PRODS 50
#define MAX_SYM_LEN 16
#define MAX_RHS_LEN 12
#define MAX_SET 64
#define MAX_STACK 128
#define MAX_TOKENS 64
#define LINE_BUF 512

#define EPSILON "eps"
#define EOF_SYM "$"

typedef struct {
    char lhs[MAX_SYM_LEN];
    char rhs[MAX_RHS_LEN][MAX_SYM_LEN];
    int rhs_len;
} Production;

typedef struct {
    char items[MAX_SET][MAX_SYM_LEN];
    int count;
} SymbolSet;

typedef struct {
    int nt_count;
    char non_terminals[MAX_NT][MAX_SYM_LEN];

    int t_count;
    char terminals[MAX_T][MAX_SYM_LEN];

    int prod_count;
    Production productions[MAX_PRODS];

    char start_symbol[MAX_SYM_LEN];

    SymbolSet first[MAX_NT];
    SymbolSet follow[MAX_NT];

    int table[MAX_NT][MAX_T + 1];
    int conflicts;
} LL1Parser;

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static int find_nt(const LL1Parser *p, const char *sym) {
    for (int i = 0; i < p->nt_count; i++) {
        if (strcmp(p->non_terminals[i], sym) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_t(const LL1Parser *p, const char *sym) {
    for (int i = 0; i < p->t_count; i++) {
        if (strcmp(p->terminals[i], sym) == 0) {
            return i;
        }
    }
    return -1;
}

static bool is_nt(const LL1Parser *p, const char *sym) {
    return find_nt(p, sym) != -1;
}

static bool is_t(const LL1Parser *p, const char *sym) {
    return find_t(p, sym) != -1;
}

static bool set_contains(const SymbolSet *s, const char *val) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->items[i], val) == 0) {
            return true;
        }
    }
    return false;
}

static bool set_add(SymbolSet *s, const char *val) {
    if (set_contains(s, val)) {
        return false;
    }
    if (s->count >= MAX_SET) {
        fprintf(stderr, "Error: SymbolSet capacity exceeded\n");
        exit(EXIT_FAILURE);
    }
    snprintf(s->items[s->count++], MAX_SYM_LEN, "%s", val);
    return true;
}

static int ensure_nt(LL1Parser *p, const char *sym) {
    int idx = find_nt(p, sym);
    if (idx != -1) {
        return idx;
    }
    if (p->nt_count >= MAX_NT) {
        fprintf(stderr, "Error: Non-terminal limit (%d) reached\n", MAX_NT);
        exit(EXIT_FAILURE);
    }
    snprintf(p->non_terminals[p->nt_count], MAX_SYM_LEN, "%s", sym);
    return p->nt_count++;
}

static void init_parser(LL1Parser *p) {
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < MAX_NT; i++) {
        for (int j = 0; j <= MAX_T; j++) {
            p->table[i][j] = -1;
        }
    }
}

static void add_production_raw(LL1Parser *p, const char *lhs, char rhs_syms[][MAX_SYM_LEN], int rhs_len) {
    if (p->prod_count >= MAX_PRODS) {
        fprintf(stderr, "Error: Production limit (%d) reached\n", MAX_PRODS);
        exit(EXIT_FAILURE);
    }
    ensure_nt(p, lhs);

    Production *prod = &p->productions[p->prod_count++];
    snprintf(prod->lhs, MAX_SYM_LEN, "%s", lhs);
    prod->rhs_len = rhs_len;
    for (int i = 0; i < rhs_len; i++) {
        snprintf(prod->rhs[i], MAX_SYM_LEN, "%s", rhs_syms[i]);
    }
}

static bool is_ident_char(unsigned char ch) {
    return isalnum(ch) || ch == '_';
}

static int tokenize_rhs(const char *alt_str, char tokens[][MAX_SYM_LEN], int max_tok) {
    char buf[LINE_BUF];
    snprintf(buf, sizeof(buf), "%s", alt_str);

    bool has_space = false;
    for (size_t i = 0; buf[i]; i++) {
        if (isspace((unsigned char)buf[i])) {
            has_space = true;
            break;
        }
    }

    int count = 0;
    if (has_space) {
        char *saveptr = NULL;
        char *tok = strtok_r(buf, " \t", &saveptr);
        while (tok && count < max_tok) {
            if (strcmp(tok, "epsilon") == 0 || strcmp(tok, "eps") == 0) {
                snprintf(tokens[count++], MAX_SYM_LEN, "%s", EPSILON);
            } else {
                snprintf(tokens[count++], MAX_SYM_LEN, "%s", tok);
            }
            tok = strtok_r(NULL, " \t", &saveptr);
        }
        return count;
    }

    size_t i = 0, len = strlen(buf);
    while (i < len && count < max_tok) {
        if (isspace((unsigned char)buf[i])) {
            i++;
            continue;
        }
        if (strncmp(&buf[i], "epsilon", 7) == 0 && !is_ident_char((unsigned char)buf[i + 7])) {
            snprintf(tokens[count++], MAX_SYM_LEN, "%s", EPSILON);
            i += 7;
        } else if (strncmp(&buf[i], "eps", 3) == 0 && !is_ident_char((unsigned char)buf[i + 3])) {
            snprintf(tokens[count++], MAX_SYM_LEN, "%s", EPSILON);
            i += 3;
        } else if (strncmp(&buf[i], "id", 2) == 0 && !is_ident_char((unsigned char)buf[i + 2])) {
            snprintf(tokens[count++], MAX_SYM_LEN, "id");
            i += 2;
        } else if (strncmp(&buf[i], "num", 3) == 0 && !is_ident_char((unsigned char)buf[i + 3])) {
            snprintf(tokens[count++], MAX_SYM_LEN, "num");
            i += 3;
        } else if (isupper((unsigned char)buf[i])) {
            char sym[MAX_SYM_LEN];
            int k = 0;
            sym[k++] = buf[i++];
            while (i < len && (buf[i] == '\'' || isdigit((unsigned char)buf[i])) && k < MAX_SYM_LEN - 1) {
                sym[k++] = buf[i++];
            }
            sym[k] = '\0';
            snprintf(tokens[count++], MAX_SYM_LEN, "%s", sym);
        } else {
            if (i + 1 < len &&
                ((buf[i] == '=' && buf[i + 1] == '=') ||
                 (buf[i] == '!' && buf[i + 1] == '=') ||
                 (buf[i] == '<' && buf[i + 1] == '=') ||
                 (buf[i] == '>' && buf[i + 1] == '='))) {
                tokens[count][0] = buf[i];
                tokens[count][1] = buf[i + 1];
                tokens[count][2] = '\0';
                count++;
                i += 2;
            } else {
                tokens[count][0] = buf[i];
                tokens[count][1] = '\0';
                count++;
                i++;
            }
        }
    }

    return count;
}

static void parse_production_line(LL1Parser *p, const char *line) {
    char buf[LINE_BUF];
    snprintf(buf, sizeof(buf), "%s", line);

    char *arrow = strstr(buf, "->");
    if (!arrow) {
        printf("  [Skip] Invalid format (missing '->'): %s\n", line);
        return;
    }
    *arrow = '\0';
    char *rhs_part = arrow + 2;

    char *lhs = buf;
    while (*lhs && isspace((unsigned char)*lhs)) lhs++;
    char *lhs_end = lhs + strlen(lhs);
    while (lhs_end > lhs && isspace((unsigned char)*(lhs_end - 1))) *(--lhs_end) = '\0';

    if (*lhs == '\0') {
        printf("  [Skip] Empty LHS in: %s\n", line);
        return;
    }

    if (p->start_symbol[0] == '\0') {
        snprintf(p->start_symbol, MAX_SYM_LEN, "%s", lhs);
    }

    char *saveptr = NULL;
    char *alt = strtok_r(rhs_part, "|", &saveptr);
    while (alt) {
        while (*alt && isspace((unsigned char)*alt)) alt++;
        char *alt_end = alt + strlen(alt);
        while (alt_end > alt && isspace((unsigned char)*(alt_end - 1))) *(--alt_end) = '\0';

        char rhs_tokens[MAX_RHS_LEN][MAX_SYM_LEN];
        int tok_count = (*alt == '\0') ? 0 : tokenize_rhs(alt, rhs_tokens, MAX_RHS_LEN);
        if (tok_count == 0) {
            snprintf(rhs_tokens[0], MAX_SYM_LEN, "%s", EPSILON);
            tok_count = 1;
        }
        add_production_raw(p, lhs, rhs_tokens, tok_count);
        alt = strtok_r(NULL, "|", &saveptr);
    }
}

static void collect_terminals(LL1Parser *p) {
    p->t_count = 0;
    for (int i = 0; i < p->prod_count; i++) {
        const Production *prod = &p->productions[i];
        for (int j = 0; j < prod->rhs_len; j++) {
            const char *sym = prod->rhs[j];
            if (strcmp(sym, EPSILON) != 0 && strcmp(sym, EOF_SYM) != 0 && !is_nt(p, sym) && !is_t(p, sym)) {
                if (p->t_count >= MAX_T) {
                    fprintf(stderr, "Error: Terminal limit (%d) reached\n", MAX_T);
                    exit(EXIT_FAILURE);
                }
                snprintf(p->terminals[p->t_count++], MAX_SYM_LEN, "%s", sym);
            }
        }
    }
}

static void first_of_sequence(const LL1Parser *p, const char alpha[][MAX_SYM_LEN], int len, SymbolSet *out) {
    out->count = 0;
    if (len == 0 || (len == 1 && strcmp(alpha[0], EPSILON) == 0)) {
        set_add(out, EPSILON);
        return;
    }

    bool nullable_prefix = true;
    for (int i = 0; i < len; i++) {
        const char *sym = alpha[i];
        if (strcmp(sym, EPSILON) == 0) {
            continue;
        }
        if (is_t(p, sym) || strcmp(sym, EOF_SYM) == 0) {
            set_add(out, sym);
            nullable_prefix = false;
            break;
        }
        int nt_idx = find_nt(p, sym);
        if (nt_idx == -1) {
            continue;
        }
        const SymbolSet *f = &p->first[nt_idx];
        bool sym_has_eps = false;
        for (int k = 0; k < f->count; k++) {
            if (strcmp(f->items[k], EPSILON) == 0) {
                sym_has_eps = true;
            } else {
                set_add(out, f->items[k]);
            }
        }
        if (!sym_has_eps) {
            nullable_prefix = false;
            break;
        }
    }

    if (nullable_prefix) {
        set_add(out, EPSILON);
    }
}

static void compute_first(LL1Parser *p) {
    bool changed;
    do {
        changed = false;
        for (int i = 0; i < p->prod_count; i++) {
            const Production *prod = &p->productions[i];
            int lhs_idx = find_nt(p, prod->lhs);
            if (lhs_idx == -1) {
                continue;
            }
            SymbolSet seq_first;
            first_of_sequence(p, prod->rhs, prod->rhs_len, &seq_first);

            for (int k = 0; k < seq_first.count; k++) {
                if (set_add(&p->first[lhs_idx], seq_first.items[k])) {
                    changed = true;
                }
            }
        }
    } while (changed);
}

static void compute_follow(LL1Parser *p) {
    compute_first(p);

    int start_idx = find_nt(p, p->start_symbol);
    if (start_idx != -1) {
        set_add(&p->follow[start_idx], EOF_SYM);
    }

    bool changed;
    do {
        changed = false;
        for (int i = 0; i < p->prod_count; i++) {
            const Production *prod = &p->productions[i];
            int lhs_idx = find_nt(p, prod->lhs);
            if (lhs_idx == -1) {
                continue;
            }

            for (int pos = 0; pos < prod->rhs_len; pos++) {
                const char *sym = prod->rhs[pos];
                int b_idx = find_nt(p, sym);
                if (b_idx == -1) {
                    continue;
                }

                int beta_len = prod->rhs_len - (pos + 1);
                if (beta_len > 0) {
                    SymbolSet beta_first;
                    first_of_sequence(p, &prod->rhs[pos + 1], beta_len, &beta_first);
                    bool beta_has_eps = false;

                    for (int k = 0; k < beta_first.count; k++) {
                        if (strcmp(beta_first.items[k], EPSILON) == 0) {
                            beta_has_eps = true;
                        } else if (set_add(&p->follow[b_idx], beta_first.items[k])) {
                            changed = true;
                        }
                    }

                    if (beta_has_eps) {
                        for (int k = 0; k < p->follow[lhs_idx].count; k++) {
                            if (set_add(&p->follow[b_idx], p->follow[lhs_idx].items[k])) {
                                changed = true;
                            }
                        }
                    }
                } else {
                    for (int k = 0; k < p->follow[lhs_idx].count; k++) {
                        if (set_add(&p->follow[b_idx], p->follow[lhs_idx].items[k])) {
                            changed = true;
                        }
                    }
                }
            }
        }
    } while (changed);
}

static int terminal_col(const LL1Parser *p, const char *term) {
    if (strcmp(term, EOF_SYM) == 0) {
        return p->t_count;
    }
    return find_t(p, term);
}

static void insert_table_entry(LL1Parser *p, int nt_idx, const char *term, int prod_idx) {
    int col = terminal_col(p, term);
    if (col < 0) {
        return;
    }
    if (p->table[nt_idx][col] != -1 && p->table[nt_idx][col] != prod_idx) {
        printf("  [!] LL(1) Conflict at M[%s, %s]: P%d vs P%d\n",
               p->non_terminals[nt_idx], term, p->table[nt_idx][col] + 1, prod_idx + 1);
        p->conflicts++;
    } else {
        p->table[nt_idx][col] = prod_idx;
    }
}

static void build_parsing_table(LL1Parser *p) {
    collect_terminals(p);
    compute_follow(p);
    p->conflicts = 0;

    for (int i = 0; i < p->prod_count; i++) {
        const Production *prod = &p->productions[i];
        int nt_idx = find_nt(p, prod->lhs);
        if (nt_idx == -1) {
            continue;
        }
        SymbolSet first_alpha;
        first_of_sequence(p, prod->rhs, prod->rhs_len, &first_alpha);

        bool has_eps = false;
        for (int k = 0; k < first_alpha.count; k++) {
            if (strcmp(first_alpha.items[k], EPSILON) == 0) {
                has_eps = true;
            } else {
                insert_table_entry(p, nt_idx, first_alpha.items[k], i);
            }
        }

        if (has_eps) {
            for (int k = 0; k < p->follow[nt_idx].count; k++) {
                insert_table_entry(p, nt_idx, p->follow[nt_idx].items[k], i);
            }
        }
    }
}

static void format_set(const SymbolSet *s, char *buf, size_t size) {
    int off = snprintf(buf, size, "{ ");
    for (int i = 0; i < s->count && off < (int)size - 1; i++) {
        off += snprintf(buf + off, size - (size_t)off, "%s%s", s->items[i], (i + 1 < s->count) ? ", " : " ");
    }
    if (off < (int)size - 1) {
        snprintf(buf + off, size - (size_t)off, "}");
    }
}

static void display_sets_and_table(const LL1Parser *p) {
    char fbuf[256], flbuf[256];

    printf("\n+------+----------------------------------+----------------------------------+\n");
    printf("| NT   | FIRST Set                        | FOLLOW Set                       |\n");
    printf("+------+----------------------------------+----------------------------------+\n");
    for (int i = 0; i < p->nt_count; i++) {
        format_set(&p->first[i], fbuf, sizeof(fbuf));
        format_set(&p->follow[i], flbuf, sizeof(flbuf));
        printf("| %-4s | %-32s | %-32s |\n", p->non_terminals[i], fbuf, flbuf);
    }
    printf("+------+----------------------------------+----------------------------------+\n");

    printf("\n=== LL(1) PARSING TABLE ===\n");
    printf("%-6s", "NT");
    for (int j = 0; j < p->t_count; j++) {
        printf(" | %-13s", p->terminals[j]);
    }
    printf(" | %-13s\n", EOF_SYM);

    for (int j = 0; j <= p->t_count + 1; j++) {
        printf("---------------");
    }
    printf("\n");

    for (int i = 0; i < p->nt_count; i++) {
        printf("%-6s", p->non_terminals[i]);
        for (int j = 0; j <= p->t_count; j++) {
            int pidx = p->table[i][j];
            if (pidx == -1) {
                printf(" | %-13s", "-");
            } else {
                const Production *pr = &p->productions[pidx];
                char buf[MAX_SYM_LEN * MAX_RHS_LEN];
                int off = snprintf(buf, sizeof(buf), "%s->", pr->lhs);
                for (int k = 0; k < pr->rhs_len && off < (int)sizeof(buf) - 1; k++) {
                    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s", pr->rhs[k]);
                }
                printf(" | %-13s", buf);
            }
        }
        printf("\n");
    }

    if (p->conflicts == 0) {
        printf("\nStatus: Grammar is LL(1) (0 conflicts).\n");
    } else {
        printf("\nStatus: Grammar has %d conflict(s) (Not strictly LL(1)).\n", p->conflicts);
    }
}

static bool parse_input_tokens(const LL1Parser *p, char tokens[][MAX_SYM_LEN], int token_count) {
    if (token_count >= MAX_TOKENS) {
        fprintf(stderr, "Error: Input token limit (%d) exceeded\n", MAX_TOKENS - 1);
        return false;
    }

    char stack[MAX_STACK][MAX_SYM_LEN];
    int top = -1;

    snprintf(stack[++top], MAX_SYM_LEN, "%s", EOF_SYM);
    snprintf(stack[++top], MAX_SYM_LEN, "%s", p->start_symbol);

    char input[MAX_TOKENS][MAX_SYM_LEN];
    for (int i = 0; i < token_count; i++) {
        snprintf(input[i], MAX_SYM_LEN, "%s", tokens[i]);
    }
    snprintf(input[token_count], MAX_SYM_LEN, "%s", EOF_SYM);
    int total_in = token_count + 1;
    int cursor = 0;
    int step = 1;

    printf("\n=== PARSE TRACE TABLE ===\n");
    printf("+------+------------------------------------+------------------------------------+----------------------------+\n");
    printf("| Step | Stack (top -> bottom)              | Input Remaining                    | Action / Table Lookup      |\n");
    printf("+------+------------------------------------+------------------------------------+----------------------------+\n");

    while (top >= 0 && cursor < total_in) {
        char stk_str[256] = {0};
        int soff = 0;
        for (int i = top; i >= 0 && soff < (int)sizeof(stk_str) - 1; i--) {
            soff += snprintf(stk_str + soff, sizeof(stk_str) - (size_t)soff, "%s%s",
                             stack[i], (i > 0) ? " " : "");
        }

        char inp_str[256] = {0};
        int ioff = 0;
        for (int i = cursor; i < total_in && ioff < (int)sizeof(inp_str) - 1; i++) {
            ioff += snprintf(inp_str + ioff, sizeof(inp_str) - (size_t)ioff, "%s%s",
                             input[i], (i + 1 < total_in) ? " " : "");
        }

        const char *X = stack[top];
        const char *a = input[cursor];

        if (strcmp(X, EOF_SYM) == 0 && strcmp(a, EOF_SYM) == 0) {
            printf("| %-4d | %-34s | %-34s | %-26s |\n", step, stk_str, inp_str, "ACCEPT");
            printf("+------+------------------------------------+------------------------------------+----------------------------+\n");
            return true;
        }

        if (strcmp(X, a) == 0) {
            char act[64];
            snprintf(act, sizeof(act), "Match '%s'", a);
            printf("| %-4d | %-34s | %-34s | %-26s |\n", step++, stk_str, inp_str, act);
            top--;
            cursor++;
        } else if (is_nt(p, X)) {
            int nt_idx = find_nt(p, X);
            int col = terminal_col(p, a);
            int pidx = (col >= 0) ? p->table[nt_idx][col] : -1;

            if (pidx == -1) {
                char act[64];
                snprintf(act, sizeof(act), "ERR: Empty M[%s, %s]", X, a);
                printf("| %-4d | %-34s | %-34s | %-26s |\n", step, stk_str, inp_str, act);
                printf("+------+------------------------------------+------------------------------------+----------------------------+\n");
                return false;
            }

            const Production *pr = &p->productions[pidx];
            char act[64];
            int aoff = snprintf(act, sizeof(act), "M[%s,%s]: %s ->", X, a, pr->lhs);
            for (int k = 0; k < pr->rhs_len && aoff < (int)sizeof(act) - 1; k++) {
                aoff += snprintf(act + aoff, sizeof(act) - (size_t)aoff, " %s", pr->rhs[k]);
            }
            printf("| %-4d | %-34s | %-34s | %-26s |\n", step++, stk_str, inp_str, act);

            top--;
            if (!(pr->rhs_len == 1 && strcmp(pr->rhs[0], EPSILON) == 0)) {
                for (int k = pr->rhs_len - 1; k >= 0; k--) {
                    if (top + 1 >= MAX_STACK) {
                        fprintf(stderr, "Error: Stack overflow\n");
                        return false;
                    }
                    snprintf(stack[++top], MAX_SYM_LEN, "%s", pr->rhs[k]);
                }
            }
        } else {
            char act[64];
            snprintf(act, sizeof(act), "ERR: Want '%s', got '%s'", X, a);
            printf("| %-4d | %-34s | %-34s | %-26s |\n", step, stk_str, inp_str, act);
            printf("+------+------------------------------------+------------------------------------+----------------------------+\n");
            return false;
        }
    }

    printf("+------+------------------------------------+------------------------------------+----------------------------+\n");
    return false;
}

static void load_default_grammar(LL1Parser *p) {
    const char *defaults[] = {
        "E -> T E'",
        "E' -> + T E' | eps",
        "T -> F T'",
        "T' -> * F T' | eps",
        "F -> ( E ) | id"
    };
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        parse_production_line(p, defaults[i]);
    }
}

int main(void) {
    LL1Parser parser;
    init_parser(&parser);

    char line[LINE_BUF];
    printf("====================================================\n");
    printf("   CONSOLE COMPILER PARSER: FIRST, FOLLOW & LL(1) \n");
    printf("====================================================\n");
    printf("Made By : Tarun Ghosh       ID: 20244103154 \n");
    printf("Enter grammar productions (one per line, e.g., E -> T E' | eps)\n");
    printf("Type 'default' to load expression grammar, or 'done' to finish:\n\n");

    while (true) {
        printf("Prod> ");
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        trim_newline(line);
        if (strcmp(line, "done") == 0) {
            break;
        }
        if (strcmp(line, "default") == 0) {
            init_parser(&parser);
            load_default_grammar(&parser);
            printf("Loaded default arithmetic expression grammar (Start: %s).\n", parser.start_symbol);
            break;
        }
        if (strlen(line) > 0) {
            parse_production_line(&parser, line);
        }
    }

    if (parser.prod_count == 0) {
        printf("No productions entered. Loading default grammar.\n");
        load_default_grammar(&parser);
    }

    printf("\nStart symbol is '%s'. Press Enter to keep or type a new one: ", parser.start_symbol);
    if (fgets(line, sizeof(line), stdin)) {
        trim_newline(line);
        if (strlen(line) > 0) {
            if (find_nt(&parser, line) != -1) {
                snprintf(parser.start_symbol, MAX_SYM_LEN, "%s", line);
            } else {
                printf("Warning: '%s' is not a non-terminal. Keeping '%s'.\n", line, parser.start_symbol);
            }
        }
    }

    build_parsing_table(&parser);
    display_sets_and_table(&parser);

    printf("\n--- STRING PARSER (type 'exit' to quit) ---\n");
    while (true) {
        printf("\nInput string (e.g., id+id*id): ");
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        trim_newline(line);
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            break;
        }
        if (strlen(line) == 0) {
            continue;
        }

        char tokens[MAX_TOKENS][MAX_SYM_LEN];
        int tok_count = tokenize_rhs(line, tokens, MAX_TOKENS);
        bool accepted = parse_input_tokens(&parser, tokens, tok_count);
        printf("Final Result: %s\n", accepted ? "ACCEPTED" : "REJECTED");
    }

    printf("\nThank You !!\n");
    return EXIT_SUCCESS;
}
