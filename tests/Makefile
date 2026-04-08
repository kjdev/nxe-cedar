CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -I ngx_compat -I ../src
CFLAGS  += $(shell pkg-config --cflags jansson)
LDFLAGS  = $(shell pkg-config --libs jansson)

SRCS = test_runner.c \
       ngx_compat/ngx_stub.c \
       ngx_compat/nxe_cedar_test_wrapper.c \
       ../src/nxe_cedar_lexer.c \
       ../src/nxe_cedar_parser.c \
       ../src/nxe_cedar_expr.c \
       ../src/nxe_cedar_eval.c

test_runner: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

test_runner_asan: $(SRCS)
	$(CC) $(CFLAGS) -fsanitize=address -fno-omit-frame-pointer -g \
		-o $@ $(SRCS) $(LDFLAGS) -fsanitize=address

test: test_runner
	./test_runner

test-asan: test_runner_asan
	./test_runner_asan

clean:
	rm -f test_runner test_runner_asan

.PHONY: test test-asan clean
