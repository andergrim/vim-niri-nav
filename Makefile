CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2
CPPFLAGS ?=
LDFLAGS ?=
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

SRC := src/vim-niri-nav.c
BIN := build/vim-niri-nav
INSTALL_NAME ?= vim-niri-nav-bin

.PHONY: all clean install

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(INSTALL_NAME)

clean:
	rm -rf build
