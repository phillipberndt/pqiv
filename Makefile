# Makefile for pqiv
#
# Dynamic settings (changed by configure script)
DESTDIR="/"
PREFIX="/usr"
OPTIONFLAGS= lib/strnatcmp.c     

# Fixed settings
REQUIRED_PACKAGES=gtk+-2.0 gthread-2.0 pango glib-2.0

# pqiv
all: pqiv manpage
pqiv: 
	$(CC) $(CFLAGS) `pkg-config --libs --cflags $(REQUIRED_PACKAGES)` -o qiv $(OPTIONFLAGS) pqiv.c
debug: 
	$(CC) $(CGLAGS) -Wall -ggdb -DDEBUG `pkg-config --libs --cflags $(REQUIRED_PACKAGES)` $(OPTIONFLAGS) -o qiv pqiv.c

# The manpage stuff is kind of hackish, but it seems that I can't rely on the C
# preprocessor (drac from gentoo reported "missing terminating ' character"
# error messages).
# The shell script strips code between '.\" unless FOO' and '.\" end' if FOO is
# defined. It's compatible with sh in POSIX mode.
manpage:
	@( IN=0; while read -r LINE; do \
		if [ "$${IN}" == "1" ]; then \
			if echo "$${LINE}" | egrep -q '.\" end'; then IN=0; continue; fi; \
			[ "$${MAT}" != "1" ] && echo "$${LINE}"; \
			continue; \
		fi; \
		REQ=$$(echo "$${LINE}" | egrep '.\" unless ' | awk '{print $$3}'); \
		[ "$${REQ}" == "" ] && { echo "$${LINE}"; continue; }; \
		MAT=$$(echo "$(OPTIONFLAGS)" | grep -q -- "-D$${REQ}" && echo 1;); \
		if [ "$${MAT}" == "1" ]; then ACT=Stripping; else ACT=Ignoring; fi; \
		echo "$${ACT} $${REQ}" >&2; \
		IN=1; \
	done < pqiv.1.template ) > qiv.1

# Cleanup
clean:
	rm -f qiv qiv.1

# Installation and uninstallation
install:
	install -D qiv $(DESTDIR)$(PREFIX)/bin/qiv
	install -D qiv.1 $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1
	link $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1 $(DESTDIR)$(PREFIX)/share/man/man1/qiv.1
uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/qiv
	rm $(DESTDIR)$(PREFIX)/share/man/man1/qiv.1
	rm $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1
mininstall:
	install -D qiv /usr/local/bin

# Package generation
PACKAGE_VERSION:=$(shell awk '/RELEASE/ {print $$3}' pqiv.c | tr -d \" | head -n1)$(SUFFIX)
package: 
	mkdir pqiv-$(PACKAGE_VERSION)/
	cp -r lib *.c pqiv.1.template gpl.txt configure Makefile README pqiv-$(PACKAGE_VERSION)/
	tar cjf pqiv-$(PACKAGE_VERSION).tbz pqiv-$(PACKAGE_VERSION)/
	rm -rf pqiv-$(PACKAGE_VERSION)
