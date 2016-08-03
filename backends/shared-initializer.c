#include <string.h>
#include <stdlib.h>
#include "../pqiv.h"

static const char *available_backends[] = {
	SHARED_BACKENDS
	NULL
};
file_type_handler_t file_type_handlers[sizeof(available_backends) / sizeof(char *)];

void initialize_file_type_handlers() {
	int i = 0;
	for(char **backend=(char **)&available_backends[0]; *backend; backend++) {
		gchar *backend_candidate = g_strdup_printf("pqiv-backend-%s.so", *backend);

		GModule *backend_module = g_module_open(backend_candidate, G_MODULE_BIND_LOCAL);
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
	if(i == 0) {
		g_printerr("Failed to load any of the image backends.\n");
		exit(1);
	}
}
