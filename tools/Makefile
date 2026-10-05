CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wno-deprecated-declarations
LDLIBS ?= -lcrypto

.PHONY: all clean check

all: rnd-decrypt

rnd-decrypt: rnd-decrypt.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

check:
	python3 -m py_compile rnet-bin2md.py

clean:
	rm -f rnd-decrypt
