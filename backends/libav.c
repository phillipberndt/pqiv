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
 * configure hint: disable-auto-configure
 */

/* This backend is based on the excellent short API example from
   http://hasanaga.info/tag/ffmpeg-libavcodec-avformat_open_input-example/ */

#include "../pqiv.h"
#include <assert.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

// This is a list of extensions that are never handled by this backend
// It is not a complete list of audio formats supported by ffmpeg,
// only those I recognized right away.
static const char * const ignore_extensions[] = {
	"aac", "ac3", "aiff", "dts", "flac", "gsm", "m4a", "mp3", "ogg", "f64be", "f64le",
	"f32be", "f32le", "s32be", "s32le", "s24be", "s24le", "s16be", "s16le", "s8",
	"u32be", "u32le", "u24be", "u24le", "u16be", "u16le", "u8", "sox", "spdif", "txt",
	"w64", "wav", "xa", "xwma", NULL
};

typedef struct {
	AVFormatContext *avcontext;
	AVCodecContext *cocontext;
	int video_stream_id;

	gboolean pkt_valid;
	AVPacket pkt;

	AVFrame *frame;
	AVFrame *rgb_frame;
	uint8_t *buffer;
} file_private_data_libav_t;

BOSNode *file_type_libav_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	file->private = g_slice_new0(file_private_data_libav_t);
	return load_images_handle_parameter_add_file(state, file);
}/*}}}*/
void file_type_libav_free(file_t *file) {/*{{{*/
	g_slice_free(file_private_data_libav_t, file->private);
}/*}}}*/
void file_type_libav_unload(file_t *file) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;

	if(private->pkt_valid) {
		av_free_packet(&(private->pkt));
		private->pkt_valid = FALSE;
	}

	if(private->frame) {
		av_frame_free(&(private->frame));
	}

	if(private->rgb_frame) {
		av_frame_free(&(private->rgb_frame));
	}

	if(private->avcontext) {
		avcodec_close(private->cocontext);
		avformat_close_input(&(private->avcontext));
	}

	if(private->buffer) {
		g_free(private->buffer);
		private->buffer = NULL;
	}
}/*}}}*/
void file_type_libav_load(file_t *file, GInputStream *data, GError **error_pointer) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;

	if(private->avcontext) {
		// Double check if the file was properly freed. It is an error if it was not, the check is merely
		// here because libav crashes if it was not.
		assert(!private->avcontext);
		file_type_libav_unload(file);
	}

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
	if(private->video_stream_id < 0 || private->avcontext->streams[private->video_stream_id]->codec->width == 0) {
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

	private->frame = av_frame_alloc();
	private->rgb_frame = av_frame_alloc();
	size_t num_bytes = avpicture_get_size(AV_PIX_FMT_RGB32, private->cocontext->width, private->cocontext->height);
	private->buffer = (uint8_t *)g_malloc(num_bytes * sizeof(uint8_t));

	file->file_flags |= FILE_FLAGS_ANIMATION;
	file->width = private->cocontext->width;
	file->height = private->cocontext->height;
	file->is_loaded = TRUE;
}/*}}}*/
double file_type_libav_animation_next_frame(file_t *file) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;

	if(!private->avcontext) {
		return -1;
	}

	if(private->pkt_valid) {
		av_free_packet(&(private->pkt));
		private->pkt_valid = FALSE;
	}

	do {
		// Loop until the next video frame is found
		memset(&(private->pkt), 0, sizeof(AVPacket));
		if(av_read_frame(private->avcontext, &(private->pkt)) < 0) {
			av_free_packet(&(private->pkt));
			avformat_seek_file(private->avcontext, -1, 0, 0, 1, 0);
			if(av_read_frame(private->avcontext, &(private->pkt)) < 0) {
				// Reading failed; end stream here to be on the safe side
				return -1;
			}
		}
	} while(private->pkt.stream_index != private->video_stream_id);

	private->pkt_valid = TRUE;

	if(private->avcontext->streams[private->video_stream_id]->avg_frame_rate.den != 0 && private->avcontext->streams[private->video_stream_id]->avg_frame_rate.num != 0) {
		// Stream has reliable average framerate
		return 1000. * private->avcontext->streams[private->video_stream_id]->avg_frame_rate.den / private->avcontext->streams[private->video_stream_id]->avg_frame_rate.num;
	}
	else if(private->avcontext->streams[private->video_stream_id]->time_base.den != 0 && private->avcontext->streams[private->video_stream_id]->time_base.num != 0) {
		// Stream has usable time base
		return private->pkt.duration * private->avcontext->streams[private->video_stream_id]->time_base.num * 1000. / private->avcontext->streams[private->video_stream_id]->time_base.den;
	}

	// TODO What could be done here as a last fallback?! -> Figure this out from ffmpeg!
	return 10;
}/*}}}*/
double file_type_libav_animation_initialize(file_t *file) {/*{{{*/
	return file_type_libav_animation_next_frame(file);
}/*}}}*/
void file_type_libav_draw(file_t *file, cairo_t *cr) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;

	if(private->pkt_valid) {
		AVFrame *frame = private->frame;
		AVFrame *rgb_frame = private->rgb_frame;

		int got_picture_ptr = 0;
		// Decode a frame
		if(avcodec_decode_video2(private->cocontext, frame, &got_picture_ptr, &(private->pkt)) >= 0 && got_picture_ptr) {
			// Prepare buffer for RGB32 version
			uint8_t *buffer = private->buffer;
			avpicture_fill((AVPicture *)rgb_frame, buffer, AV_PIX_FMT_RGB32, private->cocontext->width, private->cocontext->height);

			// Convert to RGB32
			struct SwsContext *img_convert_ctx = sws_getCachedContext(NULL, private->cocontext->width, private->cocontext->height, private->cocontext->pix_fmt, private->cocontext->width,
					private->cocontext->height, AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);
			sws_scale(img_convert_ctx, (const uint8_t * const *)((AVPicture*)frame)->data, ((AVPicture*)frame)->linesize, 0, private->cocontext->height, ((AVPicture *)rgb_frame)->data, ((AVPicture *)rgb_frame)->linesize);
			sws_freeContext(img_convert_ctx);

			// Draw to a temporary image surface and then to cr
			cairo_surface_t *image_surface = cairo_image_surface_create_for_data(rgb_frame->data[0], CAIRO_FORMAT_ARGB32, file->width, file->height, ((AVPicture *)rgb_frame)->linesize[0]);
			cairo_set_source_surface(cr, image_surface, 0, 0);
			cairo_paint(cr);
			cairo_surface_destroy(image_surface);
		}
	}
}/*}}}*/
static gboolean _is_ignored_extension(const char *extension) {/*{{{*/
	for(const char * const * ext = ignore_extensions; *ext; ext++) {
		if(strcmp(*ext, extension) == 0) {
			return TRUE;
		}
	}
	return FALSE;
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
				if(_is_ignored_extension(*fmt)) {
					continue;
				}
				gchar *format = g_strdup_printf("*.%s", *fmt);
				gtk_file_filter_add_pattern(info->file_types_handled, format);
				g_free(format);
			}
			g_strfreev(fmts);
		}

		if(iter->extensions) {
			gchar **fmts = g_strsplit(iter->extensions, ",", -1);
			for(gchar **fmt = fmts; *fmt; fmt++) {
				if(_is_ignored_extension(*fmt)) {
					continue;
				}
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
