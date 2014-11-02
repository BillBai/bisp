#include <stdio.h>
#include <stdlib.h>

#include "Assets/mpc.h"

#ifdef _WIN32

#include <string.h>

static char buffer[2048];

char* readline(char* promt)
{
    fputs(promt);
    fgets(buffer, 2048, stdin);
    char* cpy = malloc(strlen(buffer) + 1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy) - 1] = '\0';
    return cpy;
}

// Fake add_history for _WIN32
void add_history(char *input) {}

#else

#include <readline/readline.h>
#include <readline/history.h>

#endif

#define LASSERT(args, cond, err) \
    if (!(cond)) { lval_del(args); return lval_err(err); }

enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

typedef struct lval {
    int type;

    long num;
    char* err;
    char* sym;

    int count;
    struct lval** cell;
} lval;

lval* lval_num(long x)
{
    lval* v = (lval*)malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

lval* lval_err(char* m)
{
    lval* v = (lval*)malloc(sizeof(lval));
    v->type = LVAL_ERR;
    v->err = (char*)malloc(strlen(m) + 1);
    strcpy(v->err, m);
    return v;
}

lval* lval_symbol(char* s)
{
    lval* v = (lval*)malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = (char*)malloc(strlen(s) + 1);
    strcpy(v->sym, s);
    return v;
}

lval* lval_sexpr(void)
{
    lval* v = (lval*)malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

lval* lval_qexpr(void)
{
    lval* v = (lval*)malloc(sizeof(lval*));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

void lval_del(lval* v)
{
    switch (v->type) {
        case LVAL_NUM:
            break;
        case LVAL_SYM:
            free(v->sym);
            break;
        case LVAL_ERR:
            free(v->err);
            break;
        case LVAL_QEXPR:
        case LVAL_SEXPR:
            for (int i = 0; i < v->count; i++) {
                lval_del(v->cell[i]);
            }
            free(v->cell);
            break;
        default:
            break;
    }
    free(v);
}

lval* lval_add(lval* v, lval* x)
{
    v->count++;
    v->cell = (lval**)realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count-1] = x;
    return v;
}

lval* lval_pop(lval* v, int i)
{
    lval* x = v->cell[i];

    // shift the memory following the item at "i" over the the top of it.
    memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));

    // decrease the count
    v->count--;

    v->cell = (lval**)realloc(v->cell, sizeof(lval*) * (v->count));
    return x;
}

lval* lval_take(lval* v, int i)
{
    lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

lval* lval_read_num(mpc_ast_t* t)
{
    errno = 0;
    long x = strtol(t->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err("Invalid number");
}

lval* lval_read(mpc_ast_t* t)
{
    if (strstr(t->tag, "number")) { return lval_read_num(t); }
    if (strstr(t->tag, "symbol")) { return lval_symbol(t->contents); }

    // if root (>) or sexpr, cearte an empty lval_sexpr.
    lval *x = NULL;
    if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
    if (strstr(t->tag, "sexpr")) { x = lval_sexpr(); }
    if (strstr(t->tag, "qexpr")) { x = lval_qexpr(); }

    for (int i = 0; i < t->children_num; i++) {
        if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
        if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
        if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
        x = lval_add(x, lval_read(t->children[i]));
    }

    return x;
}

void lval_print(lval* v);

void lval_expr_print(lval* v, char open, char close)
{
    putchar(open);
    for (int i = 0; i < v->count; i++) {
        lval_print(v->cell[i]);

        if (i != (v->count - 1)) {
            putchar(' ');
        }
    }
    putchar(close);
}

void lval_print(lval* v)
{
    switch (v->type) {
        case LVAL_NUM:
            printf("%li", v->num);
            break;
        case LVAL_ERR:
            printf("Error: %s", v->err);
            break;
        case LVAL_SYM:
            printf("%s", v->sym);
            break;
        case LVAL_SEXPR:
            lval_expr_print(v, '(', ')');
            break;
        case LVAL_QEXPR:
            lval_expr_print(v, '{', '}');
            break;
        default:
            break;
    }
}

void lval_println(lval* v)
{
    lval_print(v); putchar('\n');
}

lval* lval_eval(lval* v);

lval* builtin_op(lval* a, char* op)
{
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number!");
        }
    }

    lval *x = lval_pop(a, 0);

    while (a->count > 0) {
        lval *y = lval_pop(a, 0);

        if (strcmp(op, "+") == 0) { x->num += y->num; }
        if (strcmp(op, "-") == 0) { x->num -= y->num; }
        if (strcmp(op, "*") == 0) { x->num *= y->num; }
        if (strcmp(op, "/") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division By zero");
                break;
            }
            x->num /= y->num;
        }
        if (strcmp(op, "%") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division By zero");
                break;
            }
            x->num %= y->num;
        }
    }

    lval_del(a);
    return x;
}

lval* builtin_head(lval* a)
{
    LASSERT(a, a->count == 1,
            "Function 'head' passed too many args!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'head' passed incorrect types!")
    LASSERT(a, a->cell[0]->count != 0,
            "Foucntion 'head' passed {}");

    lval *v = lval_take(a, 0);

    while (v->count > 1) {
        lval_del(lval_pop(v, 1));
    }

    return v;
}

lval* builtin_tail(lval* a)
{
    if (a->count != 1) {
        lval_del(a);
        return lval_err("Function 'tail' passed too many arguments! ");
    }

    if (a->cell[0]->type != LVAL_QEXPR) {
        return lval_err("Function 'tail' passed incorrect types! ");
    }

    if (a->cell[0]->count == 0) {
        return lval_err("Function 'tail' passed {}! ");
    }

    lval* v = lval_take(a, 0);

    lval_del(lval_pop(v, 0));
    return v;
}

lval* builtin_list(lval* a)
{
    a->type = LVAL_QEXPR;
    return a;
}

lval* builtin_eval(lval *a)
{
    LASSERT(a, a->count == 1,
            "Function 'eval' passed too many args");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'eval' passed too incorrect types");

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(x);
}

lval* lval_join(lval* a, lval* b)
{
    while (a->count) {
        a = lval_add(a, lval_pop(b, 0));
    }

    lval_del(b);
    return a;
}
lval* builtin_join(lval* a)
{
    for (int i = 0; i < a->count; i++) {
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
                "Function 'join' passed incorrect types!");
    }

    lval* x = lval_pop(a, 0);

    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }
    lval_del(a);
    return x;
}

lval* builtin(lval* a, char* func)
{
    if (strcmp("list", func) == 0) { return builtin_list(a); }
    if (strcmp("head", func) == 0) { return builtin_head(a); }
    if (strcmp("tail", func) == 0) { return builtin_tail(a); }
    if (strcmp("join", func) == 0) { return builtin_join(a); }
    if (strcmp("eval", func) == 0) { return builtin_eval(a); }
    if (strstr("+-*/%", func)) { return builtin_op(a, func); }
    lval_del(a);
    return lval_err("Unknow Function!");
}

lval* lval_eval_sexpr(lval* v)
{
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(v->cell[i]);
    }

    // Error checking.
    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
    }

    if (v->count == 0) { return v; }

    if (v->count == 1) { return lval_take(v, 0); }
    lval* f = lval_pop(v, 0);
    if (f->type != LVAL_SYM) {
        lval_del(f);
        lval_del(v);
        return lval_err("S-expression Does not start with symbol!");
    }

    lval* result = builtin(v, f->sym);
    lval_del(f);
    return result;
}

lval* lval_eval(lval* v)
{
    if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
    return v;
}

int main(int argc, char** argv)
{
    /* Create parsers */
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol");
    mpc_parser_t* Sexpr = mpc_new("sexpr");
    mpc_parser_t* Qexpr = mpc_new("qexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Bisp = mpc_new("bisp");

    mpca_lang(MPCA_LANG_DEFAULT,
            "                                                   \
            number : /-?[0-9]+/ ;                               \
            symbol : \"head\" | \"list\" | \"tail\" | \"join\"  \
                    | \"eval\" | '+' | '-' | '*' | '/' | '%' ;             \
            sexpr  : '(' <expr>* ')';                           \
            qexpr  : '{' <expr>* '}';                           \
            expr   : <number> | <qexpr> | <sexpr> | <symbol> ;  \
            bisp   : /^/ <expr>* /$/ ;                          \
            ",
            Number, Symbol, Sexpr, Qexpr, Expr, Bisp);

    puts("Bisp version 0.0.0.2");
    puts("Press ctrl-c to exit\n");

    while (1) {
        char *input = readline("Bisp :> ");
        add_history(input);

        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Bisp, &r)) {
            lval* result = lval_eval(lval_read(r.output));
            lval_println(result);
            lval_del(result);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        free(input);
    }

    mpc_cleanup(5, Number, Symbol, Sexpr, Qexpr, Expr, Bisp);

    return 0;
}

