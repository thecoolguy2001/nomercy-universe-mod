CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -pedantic -Werror

ENGINE   = engine/universe.c host/demo_roster.c
HEADERS  = engine/universe.h host/demo_roster.h

all: build/universe build/test_universe

build/universe: host/cli.c $(ENGINE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ host/cli.c $(ENGINE)

build/test_universe: tests/test_universe.c $(ENGINE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/test_universe.c $(ENGINE)

test: build/test_universe
	./build/test_universe

clean:
	rm -f build/universe build/test_universe

.PHONY: all test clean
