/**
 * pqiv
 *
 * Copyright (c) 2013-2015, Phillip Berndt
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
 * libav backend
 */

// TODO This crashes upon reloading a file currently

/* This backend is based on the excellent short API example from
   http://hasanaga.info/tag/ffmpeg-libavcodec-avformat_open_input-example/ */

#include "../pqiv.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

typedef struct {
	AVFormatContext *avcontext;
	AVCodecContext *cocontext;
	int video_stream_id;

	gboolean pkt_valid;
	AVPacket pkt;
} file_private_data_libav_t;

BOSNode *file_type_libav_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	file->private = g_slice_new0(file_private_data_libav_t);
	return load_images_handle_parameter_add_file(state, file);
}/*}}}*/
void file_type_libav_free(file_t *file) {/*{{{*/
	g_slice_free(file_private_data_libav_t, file->private);
}/*}}}*/
void file_type_libav_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;

	if(avformat_open_input(&(private->avcontext), file->file_name, NULL, NULL) < 0) {
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-libav-error"), 1, "Failed to load image using libav.");
		return;
	}

	private->video_stream_id = -1;
	for(size_t i=0; i<private->avcontext->nb_streams; i++) {
        if(private->avcontext->streams[i]->codec->coder_type == AVMEDIA_TYPE_VIDEO) {
			private->video_stream_id = i;
			break;
		}
	}
	if(private->video_stream_id < 0) {
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-libav-error"), 1, "This is not a video file.");
		avformat_close_input(&(private->avcontext));
		return;
	}
	private->cocontext = private->avcontext->streams[private->video_stream_id]->codec;
	AVCodec *codec = avcodec_find_decoder(private->cocontext->codec_id);
	if(!codec || avcodec_open2(private->cocontext, codec, NULL) < 0) {
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-libav-error"), 1, "Failed to open codec.");
		avformat_close_input(&(private->avcontext));
		return;
	}

	file->file_flags |= FILE_FLAGS_ANIMATION;
	file->width = private->cocontext->width;
	file->height = private->cocontext->height;
	file->is_loaded = TRUE;
}/*}}}*/
void file_type_libav_unload(file_t *file) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;

	if(private->pkt_valid) {
		av_free_packet(&(private->pkt));
		private->pkt_valid = FALSE;
	}

	if(private->avcontext) {
		avformat_close_input(&(private->avcontext));
	}
}/*}}}*/
double file_type_libav_animation_next_frame(file_t *file) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;
	if(private->pkt_valid) {
		av_free_packet(&(private->pkt));
		private->pkt_valid = FALSE;
	}

	do {
		memset(&(private->pkt), 0, sizeof(AVPacket));
		if(av_read_frame(private->avcontext, &(private->pkt)) < 0) {
			avformat_seek_file(private->avcontext, -1, 0, 0, 1, 0);
			if(av_read_frame(private->avcontext, &(private->pkt)) < 0) {
				return 0;
			}
		}
	} while(private->pkt.stream_index != private->video_stream_id);

	private->pkt_valid = TRUE;

	// TODO This is only average framerate, but there can be videos with varying framerate...
	return 1000. * private->avcontext->streams[private->video_stream_id]->avg_frame_rate.den / private->avcontext->streams[private->video_stream_id]->avg_frame_rate.num;
}/*}}}*/
double file_type_libav_animation_initialize(file_t *file) {/*{{{*/
	return file_type_libav_animation_next_frame(file);
}/*}}}*/
void file_type_libav_draw(file_t *file, cairo_t *cr) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;

	if(private->pkt_valid) {
		AVFrame frame;
		AVFrame rgb_frame;
		memset(&frame, 0, sizeof(AVFrame));

		int got_picture_ptr = 0;
		if(avcodec_decode_video2(private->cocontext, &frame, &got_picture_ptr, &(private->pkt)) >= 0 && got_picture_ptr) {
			uint8_t *buffer;
			size_t numBytes = avpicture_get_size(AV_PIX_FMT_RGB32, private->cocontext->width, private->cocontext->height);
			buffer = (uint8_t *)g_malloc(numBytes * sizeof(uint8_t));
			avpicture_fill((AVPicture *)&rgb_frame, buffer, AV_PIX_FMT_RGB32, private->cocontext->width, private->cocontext->height);

			struct SwsContext *img_convert_ctx;
			img_convert_ctx = sws_getCachedContext(NULL, private->cocontext->width, private->cocontext->height, private->cocontext->pix_fmt, private->cocontext->width, private->cocontext->height, AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL,NULL);
			sws_scale(img_convert_ctx, (const uint8_t * const *)((AVPicture*)&frame)->data, ((AVPicture*)&frame)->linesize, 0, private->cocontext->height, ((AVPicture *)&rgb_frame)->data, ((AVPicture *)&rgb_frame)->linesize);

			cairo_surface_t *image_surface = cairo_image_surface_create_for_data(rgb_frame.data[0], CAIRO_FORMAT_ARGB32, file->width, file->height, ((AVPicture *)&rgb_frame)->linesize[0]);

			cairo_set_source_surface(cr, image_surface, 0, 0);
			cairo_paint(cr);
			cairo_surface_destroy(image_surface);

			sws_freeContext(img_convert_ctx);
			g_free(buffer);
		}
	}
}/*}}}*/
void file_type_libav_initializer(file_type_handler_t *info) {/*{{{*/
    avcodec_register_all();
	av_register_all();
	avformat_network_init();

	// Register all file formats supported by libavformat
	info->file_types_handled = gtk_file_filter_new();
	for(AVInputFormat *iter = av_iformat_next(NULL); iter; iter = av_iformat_next(iter)) {
		if(iter->name) {
			gchar **fmts = g_strsplit(iter->name, ",", -1);
			for(gchar **fmt = fmts; *fmt; fmt++) {
				gchar *format = g_strdup_printf("*.%s", *fmt);
				gtk_file_filter_add_pattern(info->file_types_handled, format);
				g_free(format);
			}
			g_strfreev(fmts);
		}

		if(iter->extensions) {
			gchar **fmts = g_strsplit(iter->extensions, ",", -1);
			for(gchar **fmt = fmts; *fmt; fmt++) {
				gchar *format = g_strdup_printf("*.%s", *fmt);
				gtk_file_filter_add_pattern(info->file_types_handled, format);
				g_free(format);
			}
			g_strfreev(fmts);
		}
	}

	// Assign the handlers
	info->alloc_fn                 =  file_type_libav_alloc;
	info->free_fn                  =  file_type_libav_free;
	info->load_fn                  =  file_type_libav_load;
	info->unload_fn                =  file_type_libav_unload;
	info->animation_initialize_fn  =  file_type_libav_animation_initialize;
	info->animation_next_frame_fn  =  file_type_libav_animation_next_frame;
	info->draw_fn                  =  file_type_libav_draw;
}/*}}}*/
