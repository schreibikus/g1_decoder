/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
--         Copyright (c) 2007-2010, Hantro OY. All rights reserved.           --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#include "libav-wrapper.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AVFormatContext *pFormatCtx = NULL;
static AVCodecContext *pCodecCtx = NULL;
static AVBitStreamFilterContext *bsfc = NULL;

static int videoStream = -1;
static int rawH264 = 0;

void libav_init()
{
    av_register_all();

    av_log(NULL, AV_LOG_INFO, "Using %s\nConfig: %s\n\n", LIBAVCODEC_IDENT,
            avcodec_configuration());
}

void libav_release()
{
    if (bsfc)
        av_bitstream_filter_close(bsfc);
    if (pFormatCtx)
        avformat_close_input(&pFormatCtx);
}

int libav_open(const char *fname)
{
    /* Open video file */
    if (avformat_open_input(&pFormatCtx, fname, NULL, NULL) != 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Couldn't open file!\n");
        return -1;
    }

    /* Retrieve stream information */
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        /* this is not fatal error, yet */
        av_log(NULL, AV_LOG_ERROR, "Couldn't find stream information!\n");
    }

    /* Dump information about file onto standard error */
    av_dump_format(pFormatCtx, 0, fname, 0);

    /* Find the video stream */
    videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1,
            NULL, 0);

    if (videoStream < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Didn't find a video stream!\n");
        return -1;
    }

    /* Get a pointer to the codec context for the video stream */
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;

    if (!av_strcasecmp(pFormatCtx->iformat->long_name, "raw H.264 video"))
    {
        rawH264 = 1;
    }
    else
    {
        /* have to use a filter to get the byte stream out */
        bsfc = av_bitstream_filter_init("h264_mp4toannexb");

        if (!bsfc)
        {
            av_log(pCodecCtx, AV_LOG_ERROR,
                    "Couldn't open the h264_mp4toannexb BSF!\n");
            return -1;
        }
    }

    return 0;
}

int libav_read_frame(char *buffer, int size)
{
    AVPacket pkt;
    int ret;
    uint8_t *frame_data;
    int frame_size = 0;
    int free_data = 0;

    av_init_packet(&pkt);

    pkt.data = NULL;
    pkt.size = 0;

    do
    {
        ret = av_read_frame(pFormatCtx, &pkt);
    } while (ret == 0 && pkt.stream_index != videoStream);

    if (ret != 0)
    {
        goto end;
    }

    if (!rawH264)
    {
        uint8_t *orig_extradata = NULL;
        int orig_extradata_size = 0;

        orig_extradata_size = pCodecCtx->extradata_size;
        orig_extradata = av_mallocz(
                orig_extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

        memcpy(orig_extradata, pCodecCtx->extradata, orig_extradata_size);

        ret = av_bitstream_filter_filter(bsfc, pCodecCtx, NULL, &frame_data,
                &frame_size, pkt.data, pkt.size, 0);

        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "%s failed for stream %d, codec %s",
                    bsfc->filter->name, pkt.stream_index,
                    pCodecCtx->codec ? pCodecCtx->codec->name : "copy");
            frame_size = 0;
        }
        else if (ret > 0)
        {
            free_data = 1;
        }

        if (pCodecCtx->extradata == NULL)
        {
            pCodecCtx->extradata = orig_extradata;
            pCodecCtx->extradata_size = orig_extradata_size;
        }
        else
        {
            av_free(orig_extradata);
        }
    }
    else
    {
        frame_data = pkt.data;
        frame_size = pkt.size;
    }

    if (frame_size > 0 && frame_size <= size)
    {
        memcpy(buffer, frame_data, frame_size);
    }
    else
    {
        frame_size = 0;
    }

end:
    if (free_data)
        av_free(frame_data);

    av_free_packet(&pkt);

    return frame_size;
}

