/**
 * pqiv
 *
 * Copyright (c) 2013-2014, Phillip Berndt
 * Copyright (c) 2017, Chen John L
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
 * libwebp backend
 *
 */

#include "../pqiv.h"
#include "../lib/filebuffer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <webp/decode.h>
#include <webp/encode.h>
#include <cairo/cairo.h>


typedef struct {
	cairo_surface_t *rendered_image_surface;
} file_private_data_libwebp_t;

BOSNode *file_type_libwebp_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	file->private = g_slice_new0(file_private_data_libwebp_t);
	BOSNode *first_node = load_images_handle_parameter_add_file(state, file);
	return first_node;
}/*}}}*/

void file_type_libwebp_free(file_t *file) {/*{{{*/
	g_slice_free(file_private_data_libwebp_t, file->private);
}/*}}}*/

void file_type_libwebp_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	file_private_data_libwebp_t *private = file->private;

	// Reset the rendered_image_surface back to NULL
	if(private->rendered_image_surface) {
		cairo_surface_destroy(private->rendered_image_surface);
		private->rendered_image_surface = NULL;
	}
	
	union {
		uint32_t u32;
		uint8_t u8arr[4];
	} endian_tester;
	endian_tester.u32 = 0x12345678;
	
	int image_width, image_height;

	gsize image_size;
	GBytes *image_bytes = buffered_file_as_bytes(file, data, error_pointer);
	if(!image_bytes) {
		return;
	}
	const gchar *image_data = g_bytes_get_data(image_bytes, &image_size);
	int webp_retval = WebPGetInfo((const uint8_t*)image_data, image_size, &image_width, &image_height);
	uint8_t* webp_retptr = NULL;
	uint8_t* surface_data = NULL;
	int surface_stride = 0;;
	
	if ( webp_retval ) {
		// Create the surface
		private->rendered_image_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, image_width, image_height);
		
		surface_data = cairo_image_surface_get_data(private->rendered_image_surface);
		surface_stride = cairo_image_surface_get_stride(private->rendered_image_surface);

		cairo_surface_flush(private->rendered_image_surface);
		if ( endian_tester.u8arr[0] == 0x12 ) {
			// We are in big endian
			webp_retptr = WebPDecodeARGBInto((const uint8_t*)image_data, image_size, surface_data, surface_stride*image_height*4, surface_stride);
		} else {
			// we are in little endian
			webp_retptr = WebPDecodeBGRAInto((const uint8_t*)image_data, image_size, surface_data, surface_stride*image_height*4, surface_stride);
		}
		cairo_surface_mark_dirty(private->rendered_image_surface);
		if ( webp_retptr == NULL ) webp_retval = 0;
	}
	buffered_file_unref(file);
	image_data = NULL;
	image_size = 0;
	
	if ( !webp_retval ) {
		// Clear the rendered_image_surface if an error occurred
		if(private->rendered_image_surface) {
			cairo_surface_destroy(private->rendered_image_surface);
			private->rendered_image_surface = NULL;
		}
	
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-libwebp-error"), 1, "Failed to load image %s, malformed webp file", file->file_name);
		return;
	}
	
	/* Note that cairo's ARGB32 format requires precomputed alpha, but
	 * the output from webp is not precomputed. Therefore, we do the
	 * alpha precomputation below
	 */
	
	int i, j;
	float fR, fG, fB, fA;
	int R, G, B;
	uint32_t pixel;
	for ( i = 0; i < image_height; i++ ) {
		for ( j = 0; j < image_width; j++ ) {
			pixel = *((uint32_t*)&surface_data[i*surface_stride+j*4]);
			// Unpack into float
			fR = (pixel&0x0FF)/255.0;
			fG = ((pixel>>8)&0x0FF)/255.0;
			fB = ((pixel>>16)&0x0FF)/255.0;
			fA = ((pixel>>24)&0x0FF)/255.0;
			// Casting float to int truncates, so for rounding, we add 0.5
			R = (fR*fA*255.0+0.5);
			G = (fG*fA*255.0+0.5);
			B = (fB*fA*255.0+0.5);
			pixel = R | (G<<8) | (B<<16) | (pixel&0xFF000000);
			*((uint32_t*)&surface_data[i*surface_stride+j*4]) = pixel;
		}
	}

	file->width = image_width;
	file->height = image_height;
	file->is_loaded = TRUE;
}/*}}}*/

void file_type_libwebp_unload(file_t *file) {/*{{{*/
	file_private_data_libwebp_t *private = file->private;

	if(private->rendered_image_surface) {
		cairo_surface_destroy(private->rendered_image_surface);
		private->rendered_image_surface = NULL;
	}
}/*}}}*/

void file_type_libwebp_draw(file_t *file, cairo_t *cr) {/*{{{*/
	file_private_data_libwebp_t *private = file->private;

	if(private->rendered_image_surface) {
		cairo_set_source_surface(cr, private->rendered_image_surface, 0, 0);
		apply_interpolation_quality(cr);
		cairo_paint(cr);
	}
}/*}}}*/

void file_type_libwebp_initializer(file_type_handler_t *info) {/*{{{*/
	// Fill the file filter pattern
	info->file_types_handled = gtk_file_filter_new();
	gtk_file_filter_add_pattern(info->file_types_handled, "*.webp");

	// Assign the handlers
	info->alloc_fn                 =  file_type_libwebp_alloc;
	info->free_fn                  =  file_type_libwebp_free;
	info->load_fn                  =  file_type_libwebp_load;
	info->unload_fn                =  file_type_libwebp_unload;
	info->draw_fn                  =  file_type_libwebp_draw;
}/*}}}*/
