PREFIX=/

pqiv:
	gcc $(CFLAGS) `pkg-config --libs --cflags gtk+-2.0 gthread-2.0 pango glib` -o qiv pqiv.c

debug:
	gcc $(CGLAGS) -Wall -ggdb -DDEBUG `pkg-config --libs --cflags gtk+-2.0 gthread-2.0 pango glib` -o qiv pqiv.c

install: pqiv
	install -Ds qiv $(PREFIX)/usr/bin/qiv
	install -D qiv.1 $(PREFIX)/usr/share/man/man1/pqiv.1
	link $(PREFIX)/usr/share/man/man1/pqiv.1 $(PREFIX)/usr/share/man/man1/qiv.1

uninstall:
	rm $(PREFIX)/usr/bin/qiv
	rm $(PREFIX)/usr/share/man/man1/qiv.1
	rm $(PREFIX)/usr/share/man/man1/pqiv.1

clean:
	rm -f qiv

# Package generation
PACKAGE_VERSION=`awk '/RELEASE/ {print $$3}' pqiv.c | tr -d \" | head -n1`$(SUFFIX)
package: pqiv
	mkdir pqiv-$(PACKAGE_VERSION)/
	cp pqiv.c qiv.1 gpl.txt Makefile README pqiv-$(PACKAGE_VERSION)/
	tar cjf pqiv-$(PACKAGE_VERSION).tbz pqiv-$(PACKAGE_VERSION)/
	rm -rf pqiv-$(PACKAGE_VERSION)
