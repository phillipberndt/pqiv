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
 * libpoppler backend (PDF support)
 */

#include "../pqiv.h"
#include "../lib/filebuffer.h"
#include <poppler.h>

typedef struct {
	// The page to be displayed
	PopplerDocument *document;
	PopplerPage *page;

	// The page number, for loading
	guint page_number;
} file_private_data_poppler_t;

BOSNode *file_type_poppler_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	// We have to load the file now to get the number of pages
	GError *error_pointer = NULL;

	// TODO We cannot use poppler_document_new_from_stream due to bug
	//      https://bugs.freedesktop.org/show_bug.cgi?id=82630
	//
	//      Change this ASAP.

	GBytes *data_bytes = buffered_file_as_bytes(file, NULL);
	if(!data_bytes) {
		g_printerr("Failed to load PDF %s.\n", file->display_name);
		file_free(file);
		return NULL;
	}
	gsize data_size;
	char *data_ptr = (char *)g_bytes_get_data(data_bytes, &data_size);
	PopplerDocument *poppler_document = poppler_document_new_from_data(data_ptr, (int)data_size, NULL, &error_pointer);

	BOSNode *first_node = NULL;

	if(poppler_document) {
		int n_pages = poppler_document_get_n_pages(poppler_document);
		g_object_unref(poppler_document);

		for(int n=0; n<n_pages; n++) {
			file_t *new_file = g_new(file_t, 1);
			*new_file = *file;

			if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
				g_bytes_ref(new_file->file_data);
			}
			new_file->file_name = g_strdup(file->file_name);
			if(n == 0) {
				new_file->display_name = g_strdup(file->display_name);
			}
			else {
				new_file->display_name = g_strdup_printf("%s[%d]", file->display_name, n + 1);
			}
			new_file->private = g_new0(file_private_data_poppler_t, 1);
			((file_private_data_poppler_t *)new_file->private)->page_number = n;

			if(n == 0) {
				first_node = load_images_handle_parameter_add_file(state, new_file);
			}
			else {
				load_images_handle_parameter_add_file(state,  new_file);
			}
		}
	}

	buffered_file_unref(file);

	file_free(file);
	return first_node;
}/*}}}*/
void file_type_poppler_free(file_t *file) {/*{{{*/
	g_free(file->private);
}/*}}}*/
void file_type_poppler_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	file_private_data_poppler_t *private = file->private;

	// We need to load the data into memory, because poppler has problems with serving from streams; see above
	GBytes *data_bytes = buffered_file_as_bytes(file, data);
	gsize data_size;
	char *data_ptr = (char *)g_bytes_get_data(data_bytes, &data_size);
	private->document = poppler_document_new_from_data(data_ptr, (int)data_size, NULL, error_pointer);
	private->page = poppler_document_get_page(private->document, private->page_number);

	if(private->page) {
		double width, height;
		poppler_page_get_size(private->page, &width, &height);
		file->width = width;
		file->height = height;
		file->is_loaded = TRUE;
	}
	else {
		buffered_file_unref(file);
	}
}/*}}}*/
void file_type_poppler_unload(file_t *file) {/*{{{*/
	file_private_data_poppler_t *private = file->private;
	if(private->page) {
		g_object_unref(private->page);
		private->page = NULL;
	}
	if(private->document) {
		g_object_unref(private->document);
		private->document = NULL;

		buffered_file_unref(file);
	}
}/*}}}*/
void file_type_poppler_draw(file_t *file, cairo_t *cr) {/*{{{*/
	file_private_data_poppler_t *private = (file_private_data_poppler_t *)file->private;

	cairo_set_source_rgb(cr, 1., 1., 1.);
	cairo_paint(cr);
	poppler_page_render(private->page, cr);
}/*}}}*/

void file_type_poppler_initializer(file_type_handler_t *info) {/*{{{*/
	// Fill the file filter pattern
	info->file_types_handled = gtk_file_filter_new();
	gtk_file_filter_add_pattern(info->file_types_handled, "*.pdf");

	// Assign the handlers
	info->alloc_fn                 =  file_type_poppler_alloc;
	info->free_fn                  =  file_type_poppler_free;
	info->load_fn                  =  file_type_poppler_load;
	info->unload_fn                =  file_type_poppler_unload;
	info->draw_fn                  =  file_type_poppler_draw;
}/*}}}*/
