/**
 * pqiv
 *
 * Copyright (c) 2013-2017, Phillip Berndt
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

	#if 0 && POPPLER_CHECK_VERSION(0, 26, 5)
		// Poppler has a bug in its stream loader. The original problem #82630 was fixed in
		// http://cgit.freedesktop.org/poppler/poppler/commit/?h=poppler-0.26&id=f94ba85a736b4c90c05e7782939f32506472658e
		// and the fix appeared in 0.26.5. But there is another bug, see #96884.
		//
		GInputStream *data = image_loader_stream_file(file, NULL);
		if(!data) {
			g_printerr("Failed to load PDF %s: Error while reading file\n", file->display_name);
			file_free(file);
			return NULL;
		}
		PopplerDocument *poppler_document = poppler_document_new_from_stream(data, -1, NULL, NULL, &error_pointer);
	#else
		GBytes *data_bytes = buffered_file_as_bytes(file, NULL, &error_pointer);
		if(!data_bytes || error_pointer) {
			g_printerr("Failed to load PDF %s: %s\n", file->display_name, error_pointer->message);
			g_clear_error(&error_pointer);
			file_free(file);
			return FALSE_POINTER;
		}
		gsize data_size;
		char *data_ptr = (char *)g_bytes_get_data(data_bytes, &data_size);
		PopplerDocument *poppler_document = poppler_document_new_from_data(data_ptr, (int)data_size, NULL, &error_pointer);
	#endif

	BOSNode *first_node = NULL;

	if(poppler_document) {
		int n_pages = poppler_document_get_n_pages(poppler_document);
		g_object_unref(poppler_document);

		for(int n=0; n<n_pages; n++) {
			file_t *new_file = image_loader_duplicate_file(file,
					NULL,
					n == 0 ? NULL :  g_strdup_printf("%s[%d]", file->display_name, n + 1),
					g_strdup_printf("%s[%d]", file->sort_name, n + 1));
			new_file->private = g_slice_new0(file_private_data_poppler_t);
			((file_private_data_poppler_t *)new_file->private)->page_number = n;

			if(n == 0) {
				first_node = load_images_handle_parameter_add_file(state, new_file);
			}
			else {
				load_images_handle_parameter_add_file(state,  new_file);
			}
		}
	}
	else if(error_pointer) {
		g_printerr("Failed to load PDF %s: %s\n", file->display_name, error_pointer->message);
		g_clear_error(&error_pointer);
		first_node = FALSE_POINTER;
	}

	#if 0 && POPPLER_CHECK_VERSION(0, 26, 5)
		g_object_unref(data);
	#else
		buffered_file_unref(file);
	#endif

	if(first_node) {
		file_free(file);
	}
	return first_node;
}/*}}}*/
void file_type_poppler_free(file_t *file) {/*{{{*/
	g_slice_free(file_private_data_poppler_t, file->private);
}/*}}}*/
void file_type_poppler_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	if(error_pointer) {
		*error_pointer = NULL;
	}
	file_private_data_poppler_t *private = file->private;

	// We need to load the data into memory, because poppler has problems with serving from streams; see above
	#if 0 && POPPLER_CHECK_VERSION(0, 26, 5)
		PopplerDocument *document = poppler_document_new_from_stream(data, -1, NULL, image_loader_cancellable, error_pointer);
	#else
		GBytes *data_bytes = buffered_file_as_bytes(file, data, error_pointer);
		if(!data_bytes || (error_pointer && *error_pointer)) {
			return;
		}
		gsize data_size;
		char *data_ptr = (char *)g_bytes_get_data(data_bytes, &data_size);
		PopplerDocument *document = poppler_document_new_from_data(data_ptr, (int)data_size, NULL, error_pointer);
	#endif

	if(document) {
		PopplerPage *page = poppler_document_get_page(document, private->page_number);

		if(page) {
			double width, height;
			poppler_page_get_size(page, &width, &height);

			file->width = width;
			file->height = height;
			file->is_loaded = TRUE;
			private->page = page;
			private->document = document;
			return;
		}

		g_object_unref(document);
	}

	#if !POPPLER_CHECK_VERSION(0, 26, 5)
		buffered_file_unref(file);
	#endif
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

		#if !POPPLER_CHECK_VERSION(0, 26, 5)
			buffered_file_unref(file);
		#endif
	}
}/*}}}*/
void file_type_poppler_draw(file_t *file, cairo_t *cr) {/*{{{*/
	file_private_data_poppler_t *private = (file_private_data_poppler_t *)file->private;

	cairo_set_source_rgb(cr, 1., 1., 1.);
	cairo_paint(cr);
	apply_interpolation_quality(cr);
	poppler_page_render(private->page, cr);
}/*}}}*/

void file_type_poppler_initializer(file_type_handler_t *info) {/*{{{*/
	// Fill the file filter pattern
	info->file_types_handled = gtk_file_filter_new();
	gtk_file_filter_add_pattern(info->file_types_handled, "*.pdf");
	gtk_file_filter_add_mime_type(info->file_types_handled, "application/pdf");

	// Assign the handlers
	info->alloc_fn                 =  file_type_poppler_alloc;
	info->free_fn                  =  file_type_poppler_free;
	info->load_fn                  =  file_type_poppler_load;
	info->unload_fn                =  file_type_poppler_unload;
	info->draw_fn                  =  file_type_poppler_draw;
}/*}}}*/
