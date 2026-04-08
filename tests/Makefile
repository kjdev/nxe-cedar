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

test_runner_cov: $(SRCS)
	$(CC) $(CFLAGS) --coverage -g -O0 -o $@ $(SRCS) $(LDFLAGS) --coverage

test: test_runner
	./test_runner

test-asan: test_runner_asan
	./test_runner_asan

test-cov: test_runner_cov
	@rm -f *.gcda
	./test_runner_cov
	@echo ""
	@echo "=== Coverage Summary (src/) ==="
	@LC_ALL=C gcov -n test_runner_cov-nxe_cedar_*.gcda 2>/dev/null \
		| awk '/^File.*\.\.\/src\// { file=$$0 } \
		       /^Lines executed:/ && file { print file; print $$0; file="" }'
	@echo ""
	@echo "--- Overall ---"
	@LC_ALL=C gcov -n test_runner_cov-nxe_cedar_*.gcda 2>/dev/null \
		| awk '/^File.*\.\.\/src\// { active=1; next } \
		       /^Lines executed:/ && active { \
		           match($$0, /[0-9.]+%/); pct=substr($$0, RSTART, RLENGTH-1)+0; \
		           match($$0, /of [0-9]+/); lines=substr($$0, RSTART+3, RLENGTH-3)+0; \
		           hit += lines * pct / 100; total += lines; active=0 } \
		       END { if (total>0) printf "Lines executed: %.2f%% of %d\n", hit/total*100, total; \
		             else print "No coverage data" }'

clean:
	rm -f test_runner test_runner_asan test_runner_cov *.gcno *.gcda *.gcov

.PHONY: test test-asan test-cov clean
