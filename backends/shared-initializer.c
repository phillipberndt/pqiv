#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "../pqiv.h"

#ifndef SHARED_BACKENDS
	#warning The SHARED_BACKENDS constant is undefined. Defaulting to only gdkpixbuf support.
	#define SHARED_BACKENDS "gdkpixbuf",
#endif
#ifndef SEARCH_PATHS
	#warning The SEARCH_PATHS constant is undefined. Defaulting to only backends subdirectory.
	#define SEARCH_PATHS "backends",
#endif

static const char *available_backends[] = {
	SHARED_BACKENDS
	NULL
};
static const char *search_paths[] = {
	SEARCH_PATHS
	NULL
};
file_type_handler_t file_type_handlers[sizeof(available_backends) / sizeof(char *)];

extern char **global_argv;
static char *self_path = NULL;
static gchar *get_backend_path(const gchar *backend_name) {
	// We search for the libraries ourselves because with --enable-new-dtags
	// (default at least on Gentoo), ld writes the search path to RUNPATH
	// instead of RPATH, which dlopen() ignores.
	//
	#ifdef __linux__
	if(self_path == NULL) {
		gchar self_name[PATH_MAX];
		ssize_t name_length = readlink("/proc/self/exe", self_name, PATH_MAX);
		if(name_length >= 0) {
			self_name[name_length] = 0;
			self_path = g_strdup(dirname(self_name));
		}
	}
	#endif
	if(self_path == NULL) {
		self_path = g_strdup(dirname(global_argv[0]));
	}

	for(char **search_path=(char **)&search_paths[0]; *search_path; search_path++) {
		gchar *backend_candidate = g_strdup_printf("%s/%s/pqiv-backend-%s.so", (*search_path)[0] == '/' ? "" : self_path, *search_path, backend_name);

		if(g_file_test(backend_candidate, G_FILE_TEST_IS_REGULAR)) {
			return backend_candidate;
		}

		g_free(backend_candidate);
	}

	// As a fallback, always use the system's library lookup mechanism
	return g_strdup_printf("pqiv-backend-%s.so", backend_name);
}

void initialize_file_type_handlers(const gchar * const * disabled_backends) {
	int i = 0;
	for(char **backend=(char **)&available_backends[0]; *backend; backend++) {
		if(strv_contains(disabled_backends, *backend)) {
			continue;
		}

		gchar *backend_candidate = get_backend_path(*backend);

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
