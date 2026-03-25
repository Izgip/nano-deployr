CC = clang
CFLAGS = -Wall -Wextra -O2 -arch arm64 -arch x86_64
LDFLAGS = -lz

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

all: nanod-builder nanod-stub

nanod-builder: src/nanod-builder.c src/nanod.h
	$(CC) $(CFLAGS) -o nanod-builder src/nanod-builder.c $(LDFLAGS)

nanod-runtime.o: src/nanod-runtime.c src/nanod.h
	$(CC) $(CFLAGS) -c -o nanod-runtime.o src/nanod-runtime.c

nanod-stub: nanod-runtime.o
	$(CC) $(CFLAGS) -o nanod-stub nanod-runtime.o $(LDFLAGS)

clean:
	rm -f nanod-builder nanod-stub nanod-runtime.o

install: nanod-builder
	mkdir -p $(BINDIR)
	cp nanod-builder $(BINDIR)/nanod-builder
	chmod 755 $(BINDIR)/nanod-builder

uninstall:
	rm -f $(BINDIR)/nanod-builder

.PHONY: all clean install uninstall
