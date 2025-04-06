cc -std=c99 -Wall src/hello_world.c -ledit -o hello_world 

cc -std=c99 -Wall src/doge_parser.c src/mpc/mpc.c -o doge_parser

cc -std=c99 -Wall -g src/s_expressions.c src/mpc/mpc.c -ledit -o s_expressions