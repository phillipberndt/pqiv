#!/bin/sh
for FILE in \
	"https://raw.githubusercontent.com/sourcefrog/natsort/master/strnatcmp.c" \
	"https://raw.githubusercontent.com/sourcefrog/natsort/master/strnatcmp.h" \
	"https://raw.github.com/phillipberndt/bostree/master/bostree.c" \
	"https://raw.github.com/phillipberndt/bostree/master/bostree.h"; do

	BASENAME=`basename "$FILE"`
	wget -O $BASENAME.new $FILE && mv $BASENAME.new $BASENAME

done

git diff . || true
