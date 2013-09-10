CFLAGS=-O3
CROSS=
DESTDIR=
GTK_VERSION=0
PQIV_WARNING_FLAGS=-Wall -Wextra -Wfloat-equal -Wpointer-arith -Wcast-align -Wstrict-overflow=5 -Wwrite-strings -Waggregate-return -Wunreachable-code -Werror -Wno-unused-parameter
PREFIX=/usr
EXECUTABLE_EXTENSION=

ifeq ($(wildcard config.make),config.make)
	include config.make
endif

LIBS_GTK3=gtk+-3.0 glib-2.0 cairo gio-2.0
LIBS_GTK2=gtk+-2.0 glib-2.0 cairo gio-2.0

ifeq ($(GTK_VERSION), 0)
	ifeq ($(shell pkg-config --errors-to-stdout --print-errors $(LIBS_GTK3)), )
		LIBS=$(LIBS_GTK3)
	else
		LIBS=$(LIBS_GTK2)
	endif
endif
ifeq ($(GTK_VERSION), 2)
	LIBS=$(LIBS_GTK2)
endif
ifeq ($(GTK_VERSION), 3)
	LIBS=$(LIBS_GTK3)
endif

pqiv$(EXECUTABLE_EXTENSION): pqiv.c lib/strnatcmp.o
	$(CROSS)$(CC) $(PQIV_WARNING_FLAGS) -std=gnu99 -o $@ `$(CROSS)pkg-config --cflags $(LIBS)` $+ `$(CROSS)pkg-config --libs $(LIBS)` $(CFLAGS) $(LDFLAGS)

lib/strnatcmp.o: lib/strnatcmp.c
	$(CROSS)$(CC) -c -o $@ $+ $(CFLAGS)

install: pqiv$(EXECUTABLE_EXTENSION)
	install -D pqiv$(EXECUTABLE_EXTENSION) $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	install -D pqiv.1 $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/pqiv$(EXECUTABLE_EXTENSION)
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1

clean:
	rm -f pqiv$(EXECUTABLE_EXTENSION) lib/strnatcmp.o

distclean: clean
	rm -f config.make
