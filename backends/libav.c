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
 * libav backend
 */

/* This backend is based on the excellent short API example from
   http://hasanaga.info/tag/ffmpeg-libavcodec-avformat_open_input-example/ */

#include "../pqiv.h"
#include "../lib/filebuffer.h"
#include <assert.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55,28,1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 0, 0)
#define av_packet_unref av_free_packet
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(54, 0, 0)
#define avcodec_free_frame av_free
#define AV_PIX_FMT_RGB32 PIX_FMT_RGB32
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 41, 0)
#define AV_COMPAT_CODEC_DEPRECATED
#endif

// This is a list of extensions that are never handled by this backend
// It is not a complete list of audio formats supported by ffmpeg,
// only those I recognized right away.
static const char * const ignore_extensions[] = {
	"aac", "ac3", "aiff", "bin", "dts", "flac", "gsm", "m4a", "mp3", "ogg", "f64be", "f64le",
	"f32be", "f32le", "s32be", "s32le", "s24be", "s24le", "s16be", "s16le", "s8",
	"u32be", "u32le", "u24be", "u24le", "u16be", "u16le", "u8", "sox", "spdif", "txt",
	"w64", "wav", "xa", "xwma", NULL
};

typedef struct {
	GBytes *file_data;
	gsize file_data_pos;

	AVFormatContext *avcontext;
	AVIOContext *aviocontext;
	AVCodecContext *cocontext;
	int video_stream_id;

	gboolean pkt_valid;
	AVPacket pkt;

	AVFrame *frame;
	AVFrame *rgb_frame;
	uint8_t *buffer;

	guint pixel_width;
	guint pixel_height;
	AVRational sample_aspect_ratio;
} file_private_data_libav_t;

static int file_type_libav_memory_access_reader(void *opaque, uint8_t *buf, int buf_size) {/*{{{*/
	file_private_data_libav_t *private = opaque;

	gsize data_size = 0;
	gconstpointer data = g_bytes_get_data(private->file_data, &data_size);

	if(buf_size < 0) {
		return -1;
	}

	if((unsigned)buf_size > data_size - private->file_data_pos) {
		buf_size = data_size - private->file_data_pos;
	}

	if(private->file_data_pos < data_size) {
		memcpy(buf, (const char*)data + private->file_data_pos, buf_size);
		private->file_data_pos += buf_size;
	}

	return buf_size;
}/*}}}*/

static int64_t file_type_libav_memory_access_seeker(void *opaque, int64_t offset, int whence) {/*{{{*/
	file_private_data_libav_t *private = opaque;
	whence &= (SEEK_CUR | SEEK_SET | SEEK_END);

	gsize data_size = 0;
	g_bytes_get_data(private->file_data, &data_size);

	switch(whence) {
		case SEEK_CUR:
			if(0 <= (ssize_t)private->file_data_pos + offset && private->file_data_pos + offset < data_size) {
				private->file_data_pos += offset;
			}
			return 0;
			break;

		case SEEK_SET:
			if(offset >= 0 && offset < (ssize_t)data_size) {
				private->file_data_pos = offset;
			}
			return 0;
			break;

		case SEEK_END:
			if(offset <= 0) {
				private->file_data_pos = data_size + offset;
			}
			break;
	}

	return -1;
}/*}}}*/

BOSNode *file_type_libav_alloc(load_images_state_t state, file_t *file) {/*{{{*/
	file->private = g_slice_new0(file_private_data_libav_t);
	return load_images_handle_parameter_add_file(state, file);
}/*}}}*/
void file_type_libav_free(file_t *file) {/*{{{*/
	g_slice_free(file_private_data_libav_t, file->private);
}/*}}}*/
void file_type_libav_unload(file_t *file) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;

	if(private->file_data) {
		g_bytes_unref(private->file_data);
		buffered_file_unref(file);
		private->file_data = NULL;
		private->file_data_pos = 0;
	}

	if(private->pkt_valid) {
		av_packet_unref(&(private->pkt));
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
		#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55,53,0)
			avcodec_free_context(&(private->cocontext));
		#else
			av_freep(&(private->cocontext));
		#endif
		avformat_close_input(&(private->avcontext));
	}

	if(private->aviocontext) {
		av_freep(&private->aviocontext->buffer);
		av_freep(&private->aviocontext);
		private->aviocontext = NULL;
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

	if(file->file_flags & FILE_FLAGS_MEMORY_IMAGE) {
		if(!private->file_data) {
			private->file_data = buffered_file_as_bytes(file, data, error_pointer);
		}
		private->file_data_pos = 0;

		private->avcontext = avformat_alloc_context();
		private->aviocontext = avio_alloc_context(av_malloc(4096), 4096, 0, private, &file_type_libav_memory_access_reader, NULL, &file_type_libav_memory_access_seeker);
		private->avcontext->pb = private->aviocontext;
		if(avformat_open_input(&(private->avcontext), NULL, NULL, NULL) < 0) {
			*error_pointer = g_error_new(g_quark_from_static_string("pqiv-libav-error"), 1, "Failed to load image using libav.");
			return;
		}
	}
	else {
		if(avformat_open_input(&(private->avcontext), file->file_name, NULL, NULL) < 0) {
			*error_pointer = g_error_new(g_quark_from_static_string("pqiv-libav-error"), 1, "Failed to load image using libav.");
			return;
		}
	}

	if(avformat_find_stream_info(private->avcontext, NULL) < 0) {
		avformat_close_input(&(private->avcontext));
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-libav-error"), 1, "Failed to load image using libav.");
		return;
	}

	private->video_stream_id = -1;
	for(size_t i=0; i<private->avcontext->nb_streams; i++) {
        if(
#ifndef AV_COMPAT_CODEC_DEPRECATED
				private->avcontext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO
#else
				private->avcontext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
#endif
			) {
			private->video_stream_id = i;
			break;
		}
	}
	if(private->video_stream_id < 0 || (
#ifndef AV_COMPAT_CODEC_DEPRECATED
				private->avcontext->streams[private->video_stream_id]->codec->width == 0
#else
				private->avcontext->streams[private->video_stream_id]->codecpar->width == 0
#endif
				)) {
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-libav-error"), 1, "This is not a video file.");
		avformat_close_input(&(private->avcontext));
		return;
	}
#ifndef AV_COMPAT_CODEC_DEPRECATED
	AVCodec *codec = avcodec_find_decoder(private->avcontext->streams[private->video_stream_id]->codec->codec_id);
#else
	AVCodec *codec = avcodec_find_decoder(private->avcontext->streams[private->video_stream_id]->codecpar->codec_id);
#endif
	private->cocontext = avcodec_alloc_context3(codec);
#ifndef AV_COMPAT_CODEC_DEPRECATED
	avcodec_copy_context(private->cocontext, private->avcontext->streams[private->video_stream_id]->codec);
#else
	avcodec_parameters_to_context(private->cocontext, private->avcontext->streams[private->video_stream_id]->codecpar);
#endif
	if(!codec || avcodec_open2(private->cocontext, codec, NULL) < 0) {
		*error_pointer = g_error_new(g_quark_from_static_string("pqiv-libav-error"), 1, "Failed to open codec.");
		avformat_close_input(&(private->avcontext));
		return;
	}

	private->frame = av_frame_alloc();
	private->rgb_frame = av_frame_alloc();

	file->file_flags |= FILE_FLAGS_ANIMATION;
#ifdef AV_COMPAT_CODEC_DEPRECATED
	private->pixel_width = private->avcontext->streams[private->video_stream_id]->codecpar->width;
	private->pixel_height = private->avcontext->streams[private->video_stream_id]->codecpar->height;
	private->sample_aspect_ratio = private->avcontext->streams[private->video_stream_id]->codecpar->sample_aspect_ratio;
#else
	private->pixel_width = private->avcontext->streams[private->video_stream_id]->codec->width;
	private->pixel_height = private->avcontext->streams[private->video_stream_id]->codec->height;
	private->sample_aspect_ratio = private->avcontext->streams[private->video_stream_id]->codec->sample_aspect_ratio;
#endif
	if(private->sample_aspect_ratio.num == 0 || private->sample_aspect_ratio.den == 0) {
		private->sample_aspect_ratio.num = private->sample_aspect_ratio.den = 1;
		file->width  = private->pixel_width;
		file->height = private->pixel_height;
	}
	else if(private->sample_aspect_ratio.num > private->sample_aspect_ratio.den) {
		file->width  = private->sample_aspect_ratio.num * private->pixel_width / private->sample_aspect_ratio.den;
		file->height = private->pixel_height;
	}
	else {
		file->width  = private->pixel_width;
		file->height = private->sample_aspect_ratio.den * private->pixel_height / private->sample_aspect_ratio.num;
	}

#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(55, 0, 0)
	size_t num_bytes = avpicture_get_size(PIX_FMT_RGB32, file->width, file->height);
#else
	size_t num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32, file->width, file->height, 16);
#endif
	private->buffer = (uint8_t *)g_malloc(num_bytes * sizeof(uint8_t));

	if(file->width == 0 || file->height == 0) {
		file_type_libav_unload(file);
		file->is_loaded = FALSE;
		return;
	}
	file->is_loaded = TRUE;
}/*}}}*/
double file_type_libav_animation_next_frame(file_t *file) {/*{{{*/
	file_private_data_libav_t *private = (file_private_data_libav_t *)file->private;

	if(!private->avcontext) {
		return -1;
	}

	AVPacket old_pkt = private->pkt;

	do {
		// Loop until the next video frame is found
		memset(&(private->pkt), 0, sizeof(AVPacket));
		if(av_read_frame(private->avcontext, &(private->pkt)) < 0) {
			av_packet_unref(&(private->pkt));
			if(avformat_seek_file(private->avcontext, -1, 0, 0, 1, 0) < 0 || av_read_frame(private->avcontext, &(private->pkt)) < 0) {
				// Reading failed; end stream here to be on the safe side
				// Display last frame to the user
				private->pkt = old_pkt;
				return -1;
			}
		}
	} while(private->pkt.stream_index != private->video_stream_id);

	if(private->pkt_valid) {
		av_packet_unref(&old_pkt);
	}
	else {
		private->pkt_valid = TRUE;
	}

	AVFrame *frame = private->frame;
#ifndef AV_COMPAT_CODEC_DEPRECATED
	int got_picture_ptr = 0;
	avcodec_decode_video2(private->cocontext, frame, &got_picture_ptr, &(private->pkt));
#else
	if(avcodec_send_packet(private->cocontext, &(private->pkt)) >= 0) {
		avcodec_receive_frame(private->cocontext, frame);
	}
#endif

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

#ifdef AV_COMPAT_CODEC_DEPRECATED
		const int pix_fmt = private->avcontext->streams[private->video_stream_id]->codecpar->format;
#else
		const int pix_fmt = private->avcontext->streams[private->video_stream_id]->codec->pix_fmt;
#endif

		// Prepare buffer for RGB32 version
		uint8_t *buffer = private->buffer;
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(55, 0, 0)
		avpicture_fill((AVPicture *)rgb_frame, buffer, PIX_FMT_RGB32, file->width, file->height);
#else
		av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer, AV_PIX_FMT_RGB32, file->width, file->height, 16);
#endif

		// If a valid frame is available..
		if(frame->data[0]) {
			// ..convert to RGB32
			struct SwsContext *img_convert_ctx = sws_getCachedContext(NULL, private->pixel_width, private->pixel_height, pix_fmt, file->width,
					file->height, AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);
			sws_scale(img_convert_ctx, (const uint8_t * const*)frame->data, frame->linesize, 0, private->pixel_height, rgb_frame->data, rgb_frame->linesize);
			sws_freeContext(img_convert_ctx);
		}

		// Draw to a temporary image surface and then to cr
		cairo_surface_t *image_surface = cairo_image_surface_create_for_data(rgb_frame->data[0], CAIRO_FORMAT_ARGB32, file->width, file->height, rgb_frame->linesize[0]);
		cairo_set_source_surface(cr, image_surface, 0, 0);
		apply_interpolation_quality(cr);
		cairo_paint(cr);
		cairo_surface_destroy(image_surface);
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

	// Register as general handler for video/* MIME types
	gtk_file_filter_add_mime_type(info->file_types_handled, "video/*");

	// Assign the handlers
	info->alloc_fn                 =  file_type_libav_alloc;
	info->free_fn                  =  file_type_libav_free;
	info->load_fn                  =  file_type_libav_load;
	info->unload_fn                =  file_type_libav_unload;
	info->animation_initialize_fn  =  file_type_libav_animation_initialize;
	info->animation_next_frame_fn  =  file_type_libav_animation_next_frame;
	info->draw_fn                  =  file_type_libav_draw;
}/*}}}*/
