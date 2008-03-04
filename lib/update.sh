#!/bin/sh
wget -O strnatcmp.c.new http://sourcefrog.net/projects/natsort/strnatcmp.c && \
	grep -q "mode: c" strnatcmp.c.new && {
		if ! diff -Nru strnatcmp.c.new strnatcmp.c; then
			echo
			echo "strnatcmp.c updated"
			mv strnatcmp.c.new strnatcmp.c
		fi
}
rm -f strnatcmp.c.new

wget -O strnatcmp.h.new http://sourcefrog.net/projects/natsort/strnatcmp.h && \
	grep -q "mode: c" strnatcmp.h.new && {
		if ! diff -Nru strnatcmp.h.new strnatcmp.h; then
			echo
			echo "strnatcmp.h updated"
			mv strnatcmp.h.new strnatcmp.h
		fi
}
rm -f strnatcmp.h.new
