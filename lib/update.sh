#!/bin/sh
for FILE in \
	"http://sourcefrog.net/projects/natsort/strnatcmp.c" \
	"http://sourcefrog.net/projects/natsort/strnatcmp.h" \
	"https://raw.github.com/phillipberndt/bostree/master/bostree.c" \
	"https://raw.github.com/phillipberndt/bostree/master/bostree.h"; do

	BASENAME=`basename "$FILE"`
	wget -O $BASENAME.new $FILE && mv $BASENAME.new $BASENAME

done

git diff . || true
