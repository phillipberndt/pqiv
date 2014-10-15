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
 * ImageMagick wand backend
 *
 * TODO Multi page images support, background in PS/PDF
 */

#include "../pqiv.h"
#include "../lib/filebuffer.h"
#include <stdint.h>
#include <string.h>
#include <wand/MagickWand.h>
#include <cairo/cairo.h>

typedef struct {
	MagickWand *wand;
	cairo_surface_t *rendered_image_surface;
} file_private_data_wand_t;

// Functions to render the Magick backend to a cairo surface via in-memory PNG export
cairo_status_t file_type_wand_read_data(void *closure, unsigned char *data, unsigned int length) {/*{{{*/
	unsigned char **pos = closure;
	memcpy(data, *pos, length);
	*pos += length;
	return CAIRO_STATUS_SUCCESS;
}/*}}}*/
void file_type_wand_update_image_surface(file_t *file) {/*{{{*/
	file_private_data_wand_t *private = file->private;

	if(private->rendered_image_surface) {
		cairo_surface_destroy(private->rendered_image_surface);
		private->rendered_image_surface = NULL;
	}

	MagickSetImageFormat(private->wand, "PNG");
	size_t image_size;
	unsigned char *image_data = MagickGetImageBlob(private->wand, &image_size);
	unsigned char *image_data_loc = image_data;

	private->rendered_image_surface = cairo_image_surface_create_from_png_stream(file_type_wand_read_data, &image_data_loc);

	MagickRelinquishMemory(image_data);
}/*}}}*/

BOSNode *file_type_wand_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	file->private = g_new0(file_private_data_wand_t, 1);
	return load_images_handle_parameter_add_file(state, file);
}/*}}}*/
void file_type_wand_free(file_t *file) {/*{{{*/
	g_free(file->private);
}/*}}}*/
void file_type_wand_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	file_private_data_wand_t *private = file->private;

	private->wand = NewMagickWand();
	gsize image_size;
	GBytes *image_bytes = buffered_file_as_bytes(file, data);
	const gchar *image_data = g_bytes_get_data(image_bytes, &image_size);
	MagickBooleanType success = MagickReadImageBlob(private->wand, image_data, image_size);

	if(success == MagickFalse) {
		ExceptionType severity;
		char *message = MagickGetException(private->wand, &severity);
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-wand-error"), 1, "Failed to load image %s: %s", file->file_name, message);
		MagickRelinquishMemory(message);
		DestroyMagickWand(private->wand);
		private->wand = NULL;
		buffered_file_unref(file);
		return;
	}

	MagickResetIterator(private->wand);
	size_t delay = MagickGetImageDelay(private->wand);
	if(delay) {
		file->file_flags |= FILE_FLAGS_ANIMATION;
	}
	else {
		MagickMergeImageLayers(private->wand, MergeLayer);
		MagickResetIterator(private->wand);
	}
	MagickNextImage(private->wand);
	file_type_wand_update_image_surface(file);

	file->width = MagickGetImageWidth(private->wand);
	file->height = MagickGetImageHeight(private->wand);
	file->is_loaded = TRUE;
}/*}}}*/
double file_type_wand_animation_initialize(file_t *file) {/*{{{*/
	file_private_data_wand_t *private = file->private;
	return 1000. / MagickGetImageDelay(private->wand);
}/*}}}*/
double file_type_wand_animation_next_frame(file_t *file) {/*{{{*/
	file_private_data_wand_t *private = file->private;
	MagickBooleanType status = MagickNextImage(private->wand);
	if(status == MagickFalse) {
		MagickResetIterator(private->wand);
		MagickNextImage(private->wand);
	}
	file_type_wand_update_image_surface(file);
	return 1000. / MagickGetImageDelay(private->wand);
}/*}}}*/
void file_type_wand_unload(file_t *file) {/*{{{*/
	file_private_data_wand_t *private = file->private;

	if(private->rendered_image_surface) {
		cairo_surface_destroy(private->rendered_image_surface);
		private->rendered_image_surface = NULL;
	}

	if(private->wand) {
		DestroyMagickWand(private->wand);
		private->wand = NULL;

		buffered_file_unref(file);
	}
}/*}}}*/
void file_type_wand_draw(file_t *file, cairo_t *cr) {/*{{{*/
	file_private_data_wand_t *private = file->private;

	if(private->rendered_image_surface) {
		cairo_set_source_surface(cr, private->rendered_image_surface, 0, 0);
		cairo_paint(cr);
	}
}/*}}}*/

void file_type_wand_initializer(file_type_handler_t *info) {/*{{{*/
	// Fill the file filter pattern
	MagickWandGenesis();
	info->file_types_handled = gtk_file_filter_new();
	size_t count, i;
	char **formats = MagickQueryFormats("*", &count);
	for(i=0; i<count; i++) {
		// Skip some broken formats
		if(!strcmp(formats[i], "DJVU")) continue;              // DJVU crashes my development PC
		if(formats[i][0] != 0 && formats[i][1] == 0) continue; // One letter extensions are too random to be sure it's an image,
		                                                       // and hence they are raw format's would always succeed to load

		gchar *ext = g_ascii_strdown(formats[i], -1);
		gchar *format = g_strdup_printf("*.%s", ext);
		g_free(ext);
		gtk_file_filter_add_pattern(info->file_types_handled, format);
		g_free(format);
	}
	MagickRelinquishMemory(formats);

	// Assign the handlers
	info->alloc_fn                 =  file_type_wand_alloc;
	info->free_fn                  =  file_type_wand_free;
	info->load_fn                  =  file_type_wand_load;
	info->unload_fn                =  file_type_wand_unload;
	info->draw_fn                  =  file_type_wand_draw;
	info->animation_initialize_fn  =  file_type_wand_animation_initialize;
	info->animation_next_frame_fn  =  file_type_wand_animation_next_frame;
}/*}}}*/
