/**
 * pqiv
 *
 * Copyright (c) 2013-2014, Phillip Berndt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * libspectre backend (PS support)
 */

#include "../pqiv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libspectre/spectre.h>
#include <cairo/cairo.h>

typedef struct {
	int page_number;

	struct SpectreDocument *document;
	struct SpectrePage *page;
} file_private_data_spectre_t;

BOSNode *file_type_spectre_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	BOSNode *first_node = NULL;

	if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
		g_printerr("The spectre backend cannot load memory files\n");
		// TODO I should move this to pqiv.c
		g_free(file->file_data);
		g_free(file->display_name);
		g_free(file);
		return NULL;
	}

	struct SpectreDocument *document = spectre_document_new();
	spectre_document_load(document, file->file_name);
	int n_pages = spectre_document_get_n_pages(document);
	spectre_document_free(document);

	file_t *files_for_page = g_new0(file_t, n_pages);
	file_private_data_spectre_t *private_data_for_page = g_new0(file_private_data_spectre_t, n_pages);
	for(int n=0; n<n_pages; n++) {
		files_for_page[n] = *file;
		files_for_page[n].file_name = g_strdup(file->file_name);
		if(n == 0) {
			files_for_page[n].display_name = g_strdup(file->display_name);
		}
		else {
			files_for_page[n].display_name = g_strdup_printf("%s[%d]", file->display_name, n + 1);
		}
		files_for_page[n].private = &private_data_for_page[n];
		private_data_for_page[n].page_number = n;

		if(n == 0) {
			first_node = load_images_handle_parameter_add_file(state, &files_for_page[0]);
		}
		else {
			load_images_handle_parameter_add_file(state, &files_for_page[n]);
		}
	}

	g_free(file->display_name);
	g_free(file->file_name);
	g_free(file);

	return first_node;
}/*}}}*/
void file_type_spectre_free(file_t *file) {/*{{{*/
	g_free(file->private);
}/*}}}*/
void file_type_spectre_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	file_private_data_spectre_t *private = file->private;
	private->document = spectre_document_new();
	spectre_document_load(private->document, file->file_name);
	private->page = spectre_document_get_page(private->document, private->page_number);

	int width, height;
	spectre_page_get_size(private->page, &width, &height);
	file->width = width;
	file->height = height;

	file->is_loaded = TRUE;
}/*}}}*/
void file_type_spectre_unload(file_t *file) {/*{{{*/
	file_private_data_spectre_t *private = file->private;

	if(private->page) {
		spectre_page_free(private->page);
		private->page = NULL;
	}
	if(private->document) {
		spectre_document_free(private->document);
		private->document = NULL;
	}
}/*}}}*/
void file_type_spectre_draw(file_t *file, cairo_t *cr) {/*{{{*/
	file_private_data_spectre_t *private = (file_private_data_spectre_t *)file->private;

	SpectreRenderContext *render_context = spectre_render_context_new();
	spectre_render_context_set_scale(render_context, current_scale_level, current_scale_level);

	unsigned char *page_data = NULL;
	int row_length;
	spectre_page_render(private->page, render_context, &page_data, &row_length);

	spectre_render_context_free(render_context);

	if(spectre_page_status(private->page)) {
		g_printerr("Failed to draw image: %s\n", spectre_status_to_string(spectre_page_status(private->page)));
		if(page_data) {
			g_free(page_data);
		}
		return;
	}
	if(page_data == NULL) {
		g_printerr("Failed to draw image: Unknown error\n");
		return;
	}

	cairo_surface_t *image_surface = cairo_image_surface_create_for_data(page_data, CAIRO_FORMAT_RGB24, file->width * current_scale_level, file->height * current_scale_level, row_length);

	cairo_scale(cr, 1 / current_scale_level, 1 / current_scale_level);
	cairo_set_source_surface(cr, image_surface, 0, 0);
	cairo_paint(cr);

	cairo_surface_destroy(image_surface);
	g_free(page_data);
}/*}}}*/

void file_type_spectre_initializer(file_type_handler_t *info) {/*{{{*/
	// Fill the file filter pattern
	info->file_types_handled = gtk_file_filter_new();
	gtk_file_filter_add_pattern(info->file_types_handled, "*.ps");
	gtk_file_filter_add_pattern(info->file_types_handled, "*.eps");

	// Assign the handlers
	info->alloc_fn                 =  file_type_spectre_alloc;
	info->free_fn                  =  file_type_spectre_free;
	info->load_fn                  =  file_type_spectre_load;
	info->unload_fn                =  file_type_spectre_unload;
	info->draw_fn                  =  file_type_spectre_draw;
}/*}}}*/
