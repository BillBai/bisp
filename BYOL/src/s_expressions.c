#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include <editline/history.h>
#include <editline/readline.h>

#include "mpc/mpc.h"

typedef enum LvalType {
    LVAL_ERR,
    LVAL_NUM,
    LVAL_SYM,
    LVAL_SEXPR,
} LvalType;

typedef struct Lval {
    LvalType type;
    long num;

    // Error and Symbol types have some string data
    const char *err;
    const char *sym;

    // Count and Pointer to a list of "Lval *" 
    int count;
    struct Lval **cell;
    
} Lval;

const char *copy_str(const char *s) {
    char *s_copy = malloc(strlen(s) + 1);
    strcpy(s_copy, s);
    return s_copy;
}

Lval *lval_num(long x) {
    Lval *v = (Lval *)malloc(sizeof(Lval));
    memset(v, 0, sizeof(*v));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

Lval *lval_err(char *m) {
    Lval *v = (Lval *)malloc(sizeof(Lval));
    memset(v, 0, sizeof(*v));
    v->type = LVAL_ERR;
    v->err = copy_str(m);
    return v;
}

Lval *lval_sym(char *s) {
    Lval *v = (Lval *)malloc(sizeof(Lval));
    memset(v, 0, sizeof(*v));
    v->type = LVAL_SYM;
    v->sym = copy_str(s);
    return v;
}

Lval *lval_sexpr(void) {
    Lval *v = (Lval *)malloc(sizeof(Lval));
    memset(v, 0, sizeof(*v));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}


void lval_del(Lval *v) {
    if (!v) {
        return;
    }

    switch (v->type) {
        // Do nothing special for number type
        case LVAL_NUM: break;

        // For Err or Sym free the string data
        case LVAL_ERR: free((void *)v->err); break;
        case LVAL_SYM: free((void *)v->sym); break;

        // If Sexpr then delete all the elements inside
        case LVAL_SEXPR: {
            for (int i = 0; i < v->count; ++i) {
                lval_del(v->cell[i]);
            }
            // Also freee the memory allocated to cotain the pointers
            free((void *)v->cell);
        } break;
    }

    // free the memort allocated for the 'lval' struct itself
    free(v);
}

Lval *lval_add(Lval *v, Lval *x) {
    assert(v);
    assert(v->type = LVAL_SEXPR);
    v->count++;
    printf("lval_add realloc with %d\n", v->count);
    v->cell = realloc(v->cell, sizeof(Lval *) * v->count);
    v->cell[v->count - 1] = x;
    return v;
}

// pops the i-th cell in |v|, and returns it
Lval *lval_pop(Lval *v, int i) {
    assert(v);
    assert(i >= 0 && i < v->count);
    assert(v->type == LVAL_SEXPR);

    // find the item at "i"
    Lval *x = v->cell[i];

    // shift memory after the item at i over the top
    memmove(&(v->cell[i]), &(v->cell[i+1]), 
        (sizeof(Lval *) * v->count-i-1));
    
    // Decrease the count of items in the list
    v->count--;
    printf("lval pop realloc with %d\n", v->count);
    // Reallocate the memory used
    v->cell = realloc(v->cell, sizeof(Lval *) * v->count);

    return x;
}

// returns the i-th cell in |v|, and delete the |v|
Lval *lval_take(Lval *v, int i) {
    Lval *x = lval_pop(v, i);
    lval_del(v);
    return x;
}

void lval_print(const Lval *v);

void lval_expr_print(const Lval *v, const char open, const char close) {
    assert(v && (v->type == LVAL_SEXPR));
    putchar(open);

    for (int i = 0; i < v->count; ++i) {
        // print value contained within
        lval_print(v->cell[i]);

        // dont print tailing space if last element
        if (i != v->count - 1) {
            putchar(' ');
        }
    }

    putchar(close);
}

void lval_print(const Lval *v) {
    assert(v);
    switch (v->type) {
        case LVAL_NUM: printf("%li", v->num); break;
        case LVAL_ERR: printf("Error: %s", v->err); break;
        case LVAL_SYM: printf("%s", v->sym); break;
        case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
    }
}

void lval_println(const Lval *v) {
    lval_print(v);
    putchar('\n');
}

Lval *builtin_op(Lval *a, const char *op) {
    assert(a);
    assert(op);
    assert(a->count > 0);

    // Ensure all argumantes are numbers
    for (int i = 0; i < a->count; ++i) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number!");
        }
    }

    // pop the first element
    Lval *x = lval_pop(a, 0);

    if ((strcmp(op, "-") == 0) && a->count == 0) {
        x->num = -x->num;
    }

    // while there are still elements remaining
    while (a->count > 0) {
        Lval *y = lval_pop(a, 0);

        if (strcmp(op, "+") == 0) { x->num += y->num; }
        if (strcmp(op, "-") == 0) { x->num -= y->num; }
        if (strcmp(op, "*") == 0) { x->num *= y->num; }
        if (strcmp(op, "/") == 0) { 
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division By Zero.");
                break;
            }
            x->num /= y->num; 
        }
        if (strcmp(op, "%") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Modulo By Zero.");
                break;
            }
            x->num %= y->num; 
        }

        lval_del(y);
    }

    lval_del(a);
    return x;
}


Lval* lval_eval(Lval *v);

Lval * lval_eval_sexpr(Lval *v) {
    assert(v);
    assert(v->type == LVAL_SEXPR);

    // Evaluate Childred
    for (int i = 0; i < v->count; ++i) {
        v->cell[i] = lval_eval(v->cell[i]);
    }

    // Error Checking
    for (int i = 0; i < v->count; ++i) {
        if (v->cell[i]->type == LVAL_ERR) {
            return lval_take(v, i);
        }
    }

    // Empty Expression
    if (v->count == 0) {
        return v;
    }

    // Single Expression
    if (v->count == 1) {
        return lval_take(v, 0);
    }

    Lval *f = lval_pop(v, 0);
    if (f->type != LVAL_SYM) {
        lval_del(f);
        lval_del(v);
        return lval_err("S-expression Does not start with symbol.");
    }

    Lval *result = builtin_op(v, f->sym);
    lval_del(f);
    return result;
}

Lval *lval_eval(Lval *v) {
    assert(v);
    // Evaluate S-Expressions
    if (v->type == LVAL_SEXPR) {
        return lval_eval_sexpr(v);
    }

    // All other lval types remain the same
    return v;
}


Lval *lval_read_num(mpc_ast_t *t) {
    errno = 0;
    long x = strtol(t->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

Lval *lval_read(mpc_ast_t* t) {
    if (strstr(t->tag, "number")) {
        return lval_read_num(t);
    }

    if (strstr(t->tag, "symbol")) {
        return lval_sym(t->contents);
    }

    Lval *x = NULL;
    if (strcmp(t->tag, ">") == 0) {
        x = lval_sexpr();
    }

    if (strstr(t->tag, "sexpr")) {
        x = lval_sexpr();
    }

    for (int i = 0; i < t->children_num; ++i) {
        mpc_ast_t *c = t->children[i];
        if (strcmp(c->contents, "(") == 0) { 
            continue; 
        }

        if (strcmp(c->contents, ")") == 0) { 
            continue; 
        }

        if (strcmp(c->tag, "regex") == 0) { 
            continue; 
        }

        x = lval_add(x, lval_read(c));
    }
    return x;
}


int main(int argc, const char *argv[]) {
    // define parsers
    mpc_parser_t *Number = mpc_new("number");
    mpc_parser_t *Symbol = mpc_new("symbol");
    mpc_parser_t *Sexpr = mpc_new("sexpr");
    mpc_parser_t *Expr = mpc_new("expr");
    mpc_parser_t *Lispy = mpc_new("lispy");

    mpca_lang(MPCA_LANG_DEFAULT,
    "                                            \
      number  : /-?[0-9]+/ ;                     \
      symbol  : '+' | '-' | '*' | '/' | '%';    \
      sexpr   : '(' <expr>* ')' ;                \
      expr    : <number> | <symbol> | <sexpr> ;  \
      lispy   : /^/ <expr>* /$/ ;                \
      ",
    Number, Symbol, Sexpr, Expr, Lispy);

    puts("Lispy Version 0.0.0.0.5");
    puts("Press Ctrl-C to Exit\n");

    while (1) {
        const char *input = readline("lispy> ");
        add_history(input);

        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Lispy, &r)) {
            // mpc_ast_print(r.output);
            Lval *x = lval_eval(lval_read(r.output));
            lval_println(x);
            lval_del(x);
            mpc_ast_delete(r.output);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        free((char *)input);
    }

    mpc_cleanup(5, Number, Symbol, Sexpr, Expr, Lispy);

    return 0;
}