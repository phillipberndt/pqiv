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
#include <poppler.h>

typedef struct {
	// The byte data from the document; must apparently be kept in memory
	// for poppler to work properly
	GBytes *data;

	// The page to be displayed
	PopplerDocument *document;
	PopplerPage *page;

	// The page number, for loading
	guint page_number;
} file_private_data_poppler_t;

BOSNode *file_type_poppler_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	// We have to load the file now to get the number of pages
	GError *error_pointer = NULL;

	GInputStream *data_stream = image_loader_stream_file(file, &error_pointer);
	BOSNode *first_node = NULL;

	if(data_stream) {
		#if POPPLER_CHECK_VERSION(0, 22, 0)
			PopplerDocument *poppler_document = poppler_document_new_from_stream(data_stream, -1, NULL, NULL, &error_pointer);
		#else
			GBytes *data_bytes = g_input_stream_read_completely(data_stream, image_loader_cancellable, &error_pointer);
			gsize data_size;
			char *data_ptr = (char *)g_bytes_get_data(data_bytes, &data_size);
			PopplerDocument *poppler_document = poppler_document_new_from_data(data_ptr, (int)data_size, NULL, &error_pointer);
		#endif

		if(poppler_document) {
			int n_pages = poppler_document_get_n_pages(poppler_document);
			g_object_unref(poppler_document);

			file_t *files_for_page = g_new0(file_t, n_pages);
			file_private_data_poppler_t *private_data_for_page = g_new0(file_private_data_poppler_t, n_pages);
			for(int n=0; n<n_pages; n++) {
				files_for_page[n] = *file;
				if((file->file_flags & FILE_FLAGS_MEMORY_IMAGE)) {
					g_bytes_ref(files_for_page[n].file_data);
				}
				else {
					files_for_page[n].file_name = g_strdup(file->file_name);
				}
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
		}

		#if !POPPLER_CHECK_VERSION(0, 22, 0)
			g_bytes_unref(data_bytes);
		#endif
		g_object_unref(data_stream);
	}

	if(error_pointer) {
		g_printerr("Failed to load PDF %s: %s\n", file->display_name, error_pointer->message);
	}

	g_free(file);

	return first_node;
}/*}}}*/
void file_type_poppler_free(file_t *file) {/*{{{*/
	g_free(file->private);
}/*}}}*/
void file_type_poppler_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	file_private_data_poppler_t *private = file->private;

	// We need to load the data into memory, because poppler has problems with serving from streams
	private->data = g_input_stream_read_completely(data, image_loader_cancellable, error_pointer);
	gsize data_size;
	char *data_ptr = (char *)g_bytes_get_data(private->data, &data_size);
	private->document = poppler_document_new_from_data(data_ptr, (int)data_size, NULL, error_pointer);
	private->page = poppler_document_get_page(private->document, private->page_number);

	if(private->page) {
		double width, height;
		poppler_page_get_size(private->page, &width, &height);
		file->width = width;
		file->height = height;
		file->is_loaded = TRUE;
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
	}
	if(private->data) {
		g_bytes_unref(private->data);
		private->data = NULL;
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
