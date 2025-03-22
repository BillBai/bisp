#include <stdio.h>
#include <stdlib.h>

#include <editline/readline.h>
#include <editline/history.h>


int main(int argc, char *argv[]) {
    puts("Bisp Version 0.0.0\n");
    puts("Press Ctrl+C to Exit\n");

    while (1) {
        char *input = readline("Bisp> ");

        add_history(input);

        free(input);
    }

    return 0;
}