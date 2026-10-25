#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <stdio.h>
#include <stdint.h>

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
    FILE *pFile;
    char szFilename[32];

    snprintf(szFilename, sizeof(szFilename), "frame%d.ppm", iFrame);
    pFile = fopen(szFilename, "wb");
    if (pFile == NULL) return;

    fprintf(pFile, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; y++)
        fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);
    fclose(pFile);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <video file>\n", argv[0]);
        return -1;
    }

    AVFormatContext *pFormatCtx = NULL;
    if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0) return -1;
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) return -1;
    av_dump_format(pFormatCtx, 0, argv[1], 0);

    int videoStream = -1;
    for (unsigned i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }
    if (videoStream == -1) return -1;

    AVCodecParameters *par = pFormatCtx->streams[videoStream]->codecpar;
    const AVCodec *pCodec = avcodec_find_decoder(par->codec_id);
    if (!pCodec) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }

    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    if (avcodec_parameters_to_context(pCodecCtx, par) < 0) {
        fprintf(stderr, "Couldn't copy codec parameters\n");
        return -1;
    }
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) return -1;

    AVFrame *pFrame = av_frame_alloc();
    AVFrame *pFrameRGB = av_frame_alloc();
    if (!pFrame || !pFrameRGB) return -1;

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24,
                                            pCodecCtx->width, pCodecCtx->height, 1);
    uint8_t *buffer = (uint8_t *)av_malloc(numBytes);
    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer,
                         AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height, 1);

    struct SwsContext *sws_ctx = sws_getContext(
        pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL);

    AVPacket *packet = av_packet_alloc();
    int i = 0;

    while (av_read_frame(pFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoStream) {
            if (avcodec_send_packet(pCodecCtx, packet) >= 0) {
                while (avcodec_receive_frame(pCodecCtx, pFrame) >= 0) {
                    sws_scale(sws_ctx, (const uint8_t *const *)pFrame->data,
                              pFrame->linesize, 0, pCodecCtx->height,
                              pFrameRGB->data, pFrameRGB->linesize);
                    if (++i <= 5)
                        SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
                }
            }
        }
        av_packet_unref(packet);
        if (i >= 5) break;   // stop once we have the frames we want
    }

    av_packet_free(&packet);
    sws_freeContext(sws_ctx);
    av_free(buffer);
    av_frame_free(&pFrameRGB);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecCtx);
    avformat_close_input(&pFormatCtx);
    return 0;
}