#!/usr/bin/env python3
import sys

#this could be more elegant, but for now it's line by line translation of code from GNUmakefile to python
with open("initializer.c", "w", encoding="utf-8") as f:
    f.write("/* Auto-Generated file by generate_initializer.py */\n")
    f.write("#include \"../pqiv.h\"\n")
    f.write(f"file_type_handler_t file_type_handlers[{len(sys.argv)}];\n")
    backends = sys.argv[1:]
    backends.sort()
    for backend in backends:
        f.write(f"void file_type_{backend}_initializer(file_type_handler_t *info);\n")
    f.write("void initialize_file_type_handlers(const gchar * const * disabled_backends) {\n")
    f.write("   int i = 0;\n")
    if 'gdkpixbuf' in backends:
        f.write("   if(!strv_contains(disabled_backends, \"gdkpixbuf\")) file_type_gdkpixbuf_initializer(&file_type_handlers[i++]);\n")
        backends.remove('gdkpixbuf')
    for backend in backends:
        f.write(f"   if(!strv_contains(disabled_backends, \"{backend}\")) file_type_{backend}_initializer(&file_type_handlers[i++]);\n")
    f.write("}\n")
