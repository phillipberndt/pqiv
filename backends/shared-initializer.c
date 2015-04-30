#include <string.h>
#include "../pqiv.h"

static const char *available_backends = BACKENDS;
file_type_handler_t file_type_handlers[BACKEND_COUNT + 1];

void initialize_file_type_handlers() {
	int i = 0;
	gchar **backends = g_strsplit(available_backends, " ", 0);
	for(gchar **backend=backends; *backend; backend++) {
		gchar *backend_candidate = g_strdup_printf("pqiv-backend-%s.so", *backend);

		GModule *backend_module = g_module_open(backend_candidate, 0);
		if(backend_module) {
			gchar *backend_initializer = g_strdup_printf("file_type_%s_initializer", *backend);

			file_type_initializer_fn_t initializer;
			if(g_module_symbol(backend_module, backend_initializer, (gpointer *)&initializer)) {
				initializer(&file_type_handlers[i++]);
				g_module_make_resident(backend_module);
			}

			g_free(backend_initializer);
			g_module_close(backend_module);
		}

		g_free(backend_candidate);
	}
	g_strfreev(backends);
}
