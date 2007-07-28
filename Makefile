PREFIX=/usr

pqiv:
	$(CC) $(CFLAGS) `pkg-config --libs --cflags gtk+-2.0 gthread-2.0 pango glib` -o qiv pqiv.c

debug:
	$(CC) $(CGLAGS) -Wall -ggdb -DDEBUG `pkg-config --libs --cflags gtk+-2.0 gthread-2.0 pango glib` -o qiv pqiv.c

install: pqiv
	install -D qiv $(DESTDIR)$(PREFIX)/bin/qiv
	install -D qiv.1 $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1
	link $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1 $(DESTDIR)$(PREFIX)/share/man/man1/qiv.1

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/qiv
	rm $(DESTDIR)$(PREFIX)/share/man/man1/qiv.1
	rm $(DESTDIR)$(PREFIX)/share/man/man1/pqiv.1

clean:
	rm -f qiv

# Package generation
PACKAGE_VERSION=`awk '/RELEASE/ {print $$3}' pqiv.c | tr -d \" | head -n1`$(SUFFIX)
package: pqiv
	mkdir pqiv-$(PACKAGE_VERSION)/
	cp pqiv.c qiv.1 gpl.txt Makefile README pqiv-$(PACKAGE_VERSION)/
	tar cjf pqiv-$(PACKAGE_VERSION).tbz pqiv-$(PACKAGE_VERSION)/
	rm -rf pqiv-$(PACKAGE_VERSION)
