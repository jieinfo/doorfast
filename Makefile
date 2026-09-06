CC ?= cc
CFLAGS := -std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests
TEST_SOURCES := tests/test_main.c tests/test_config.c tests/test_sip.c tests/test_policy.c src/config.c src/event.c src/sip.c src/session.c src/policy.c

.PHONY: test clean

test: build/doorfast-tests

build/doorfast-tests: $(TEST_SOURCES) tests/test.h src/doorfast.h

build:

	@mkdir -p build

build/doorfast-tests: | build
	$(CC) $(CFLAGS) $(TEST_SOURCES) -o $@
	$@

clean:
	rm -rf build
