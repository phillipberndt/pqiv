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
#include <glib/gstdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libspectre/spectre.h>
#include <cairo/cairo.h>

typedef struct {
	char *temporary_local_file;

	int page_number;

	struct SpectreDocument *document;
	struct SpectrePage *page;
} file_private_data_spectre_t;

char *spectre_ensure_local_file(file_t *file) {/*{{{*/
	file_private_data_spectre_t *private = file->private;

	if(private->temporary_local_file) {
		// Already went through this: Simply return the stored filename
		return g_strdup(private->temporary_local_file);
	}

	GFile *input_file = g_file_new_for_commandline_arg(file->file_name);
	gchar *path = g_file_get_path(input_file);
	g_object_unref(input_file);

	if(path) {
		// Local file, just return the name
		return path;
	}

	GInputStream *data = image_loader_stream_file(file, NULL);
	if(!data) {
		return NULL;
	}

	GFile *temporary_file;
	GFileIOStream *iostream = NULL;
	gchar *extension = strrchr(file->file_name, '.');
	if(extension) {
		gchar *name_template = g_strdup_printf("pqiv-XXXXXX%s", extension);
		temporary_file = g_file_new_tmp(name_template, &iostream, NULL);
		g_free(name_template);
	}
	else {
		temporary_file = g_file_new_tmp("pqiv-XXXXXX.ps", &iostream, NULL);
	}
	if(!temporary_file) {
		return NULL;
	}

	g_output_stream_splice(g_io_stream_get_output_stream(G_IO_STREAM(iostream)), data, G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, image_loader_cancellable, NULL);
	private->temporary_local_file = g_file_get_path(temporary_file);

	g_object_unref(iostream);
	g_object_unref(temporary_file);

	return g_strdup(private->temporary_local_file);
}/*}}}*/

BOSNode *file_type_spectre_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	BOSNode *first_node = NULL;

	if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
		g_printerr("The spectre backend cannot load memory files\n");
		file_free(file);
		return NULL;
	}

	// Load the document to get the number of pages
	struct SpectreDocument *document = spectre_document_new();
	file_private_data_spectre_t private;
	private.temporary_local_file = NULL;
	file->private = &private;
	char *file_name = spectre_ensure_local_file(file);
	spectre_document_load(document, file_name);
	g_free(file_name);
	if(spectre_document_status(document)) {
		g_printerr("Failed to load image %s: %s\n", file->file_name, spectre_status_to_string(spectre_document_status(document)));
		spectre_document_free(document);
		if(private.temporary_local_file) {
			g_unlink(private.temporary_local_file);
			g_free(private.temporary_local_file);
		}
		file_free(file);
		return NULL;
	}
	int n_pages = spectre_document_get_n_pages(document);
	spectre_document_free(document);

	for(int n=0; n<n_pages; n++) {
		file_t *new_file = g_new(file_t, 1);
		*new_file = *file;

		new_file->file_name = g_strdup(file->file_name);
		if(n == 0) {
			new_file->display_name = g_strdup(file->display_name);
		}
		else {
			new_file->display_name = g_strdup_printf("%s[%d]", file->display_name, n + 1);
		}

		new_file->private = g_new0(file_private_data_spectre_t, 1);
		((file_private_data_spectre_t *)new_file->private)->page_number = n;

		if(n == 0) {
			first_node = load_images_handle_parameter_add_file(state, new_file);
		}
		else {
			load_images_handle_parameter_add_file(state, new_file);
		}
	}

	if(private.temporary_local_file) {
		g_unlink(private.temporary_local_file);
		g_free(private.temporary_local_file);
	}
	file->private = NULL;
	file_free(file);
	return first_node;
}/*}}}*/
void file_type_spectre_free(file_t *file) {/*{{{*/
	g_free(file->private);
}/*}}}*/
void file_type_spectre_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	file_private_data_spectre_t *private = file->private;

	private->document = spectre_document_new();
	gchar *file_name = spectre_ensure_local_file(file);
	spectre_document_load(private->document, file_name);
	g_free(file_name);
	if(spectre_document_status(private->document)) {
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-spectre-error"), 1, "Failed to load image %s: %s\n", file->file_name, spectre_status_to_string(spectre_document_status(private->document)));
		return;
	}
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
	if(private->temporary_local_file) {
		g_unlink(private->temporary_local_file);
		g_free(private->temporary_local_file);
		private->temporary_local_file = NULL;
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
