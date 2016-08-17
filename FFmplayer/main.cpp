//
//  main.cpp
//  FFmplayer
//
//  Created by 冯伟伦 on 16/8/16.
//  Copyright © 2016年 冯伟伦. All rights reserved.
//

#include <stdio.h>
extern "C"
{
//ffmpeg
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
//sdl2
#include <SDL2/SDL.h>
}

int bThreadExit;
int bThreadPause;

int sfp_refresh_thread(void *opaque)
{
    bThreadExit = 0;
    bThreadPause = 0;
    SDL_Event event;
    while(!bThreadExit)
    {
        if(!bThreadPause)
        {
            //refresh event
            event.type = SDL_USEREVENT + 1;
            SDL_PushEvent(&event);
        }
        SDL_Delay(40);
    }
    bThreadExit = 0;
    bThreadPause = 0;
    
    //break event
    event.type = SDL_USEREVENT + 2;
    SDL_PushEvent(&event);
    return 0;
}

int main_multi_thread(int argc, const char *argv[])
{
    AVFormatContext *pFormatCtx = NULL;
    AVCodecParameters *pCodecPmts = NULL;
    AVCodec *pCodec = NULL;
    AVFrame *pFrame = NULL;
    AVFrame *pFrameYUV= NULL;
    AVPacket *pPacket;
    uint8_t *out_buffer;
    int i_videoidx;
    int y_size;
    int ret;
    int got_picture;
    struct SwsContext *img_convert_ctx;
    int i;
    
    char filepath[] = "/Users/fengweilun/Desktop/RaceHorses_416x240_30.h265";
    
    int screen_w = 0;
    int screen_h = 0;
    SDL_Window *pSDLWd;
    SDL_Renderer *pSDLRd;
    SDL_Texture *pSDLTx;
    SDL_Rect sSDLRect;
    SDL_Thread *pVideoTid;
    SDL_Event event;
    
    FILE *fp_yuv;
    //读取码流，得到码流的信息以及编码格式，确定要使用的编/解码器
    av_register_all();
    avformat_network_init();
    pFormatCtx = avformat_alloc_context();
    if(0 != avformat_open_input(&pFormatCtx, filepath, NULL, NULL))
    {
        printf("open input stream fail\n");
        return -1;
    }
    if(0 > avformat_find_stream_info(pFormatCtx, NULL))
    {
        printf("find error\n");
        return -1;
    }
    i_videoidx = -1;
    for(i = 0; i < pFormatCtx->nb_streams; i++)
        if(pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            i_videoidx = i;
            break;
        }
    if(-1 == i_videoidx)
    {
        printf("Don't know which one is video\n");
        return -1;
    }
    pCodecPmts = pFormatCtx->streams[i_videoidx]->codecpar;
    pCodec = avcodec_find_decoder(pCodecPmts->codec_id);
    if(NULL == pCodec)
    {
        printf("No Codec Found\n");
        return -1;
    }
    if(avcodec_open2(pFormatCtx->streams[i_videoidx]->codec, pCodec, NULL) < 0)
    {
        printf("Cannot Open Codec\n");
        return -1;
    }
    
    //为每一帧分配内存空间
    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();
    out_buffer = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecPmts->width, pCodecPmts->height, 1));
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, pCodecPmts->width, pCodecPmts->height, 1);//利用一个out_buffer来填充pFrameYUV的空间
    
    pPacket = (AVPacket *)av_malloc(sizeof(AVPacket));//给packet分配信息
    printf("------------File Infomation-----------\n");
    av_dump_format(pFormatCtx, 0, filepath, 0);//打印codec信息
    printf("--------------------------------------\n");
    img_convert_ctx = sws_getContext(pCodecPmts->width, pCodecPmts->height, pFormatCtx->streams[i_videoidx]->codec->pix_fmt, pCodecPmts->width, pCodecPmts->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);//转码句柄，确定转码需要的所有工具。
    
    //初始化SDL
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        printf("could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    
    screen_w = pCodecPmts->width;
    screen_h = pCodecPmts->height;
    //初始化SDL的Window
    pSDLWd = SDL_CreateWindow("FFmplayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL);
    if(!pSDLWd)
    {
        printf("window init fail\n");
        return -1;
    }
    pSDLRd = SDL_CreateRenderer(pSDLWd, -1, 0);//初始化SDLRender
    pSDLTx = SDL_CreateTexture(pSDLRd, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);//初始化SDLTexture
    sSDLRect.x = 0;
    sSDLRect.y = 0;
    sSDLRect.w = screen_w;
    sSDLRect.h = screen_h;//初始化SDLRectangle
    
    pVideoTid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);//窗口显示
    
    while(1)
    {
        SDL_WaitEvent(&event);

        if (event.type == SDL_USEREVENT + 1)
        {
            while(1)
            {
                if(av_read_frame(pFormatCtx, pPacket) < 0)
                    bThreadExit=1;
                
                if(pPacket->stream_index==i_videoidx)
                    break;
            }
            if(bThreadExit)
                break;
            ret = avcodec_decode_video2(pFormatCtx->streams[i_videoidx]->codec, pFrame, &got_picture, pPacket);
            if(ret < 0){
                printf("Decode Error.\n");
                return -1;
            }
            if(got_picture)
            {
                sws_scale(img_convert_ctx, (const uint8_t * const *)pFrame->data, pFrame->linesize, 0, pCodecPmts->height, pFrameYUV->data, pFrameYUV->linesize);//图像大小以及格式转换。
                SDL_UpdateYUVTexture(pSDLTx, &sSDLRect, pFrameYUV->data[0], pFrameYUV->linesize[0], pFrameYUV->data[1], pFrameYUV->linesize[1], pFrameYUV->data[2], pFrameYUV->linesize[2]);//将YUV上传到Texture
                SDL_RenderClear(pSDLRd);
                SDL_RenderCopy(pSDLRd, pSDLTx, NULL, &sSDLRect);//将Texture利用一个Rectangle利用传给render
                SDL_RenderPresent(pSDLRd);//Render负责显示，传给picture
            }
        }
        else if(event.type == SDL_KEYDOWN)
        {
            if(event.key.keysym.sym == SDLK_SPACE)
                bThreadPause = !bThreadPause;
        }
        else if(event.type == SDL_QUIT)
        {
            bThreadExit = 1;
        }
        else if(SDL_USEREVENT + 2 == event.type)
            break;
    }
    
    av_packet_unref(pPacket);
    while(1)//flush剩下的帧，通过unref这个packet实现传入为空，实现flush。
    {
        ret = avcodec_decode_video2(pFormatCtx->streams[i_videoidx]->codec, pFrame, &got_picture, pPacket);
        if(ret < 0 || !got_picture)
            break;
        sws_scale(img_convert_ctx, (const uint8_t * const *)pFrame->data, pFrame->linesize, 0, pCodecPmts->height, pFrameYUV->data, pFrameYUV->linesize);
        SDL_UpdateYUVTexture(pSDLTx, &sSDLRect, pFrameYUV->data[0], pFrameYUV->linesize[0], pFrameYUV->data[1], pFrameYUV->linesize[1], pFrameYUV->data[2], pFrameYUV->linesize[2]);
        SDL_RenderClear(pSDLRd);
        SDL_RenderCopy(pSDLRd, pSDLTx, NULL, &sSDLRect);
        SDL_RenderPresent(pSDLRd);
        SDL_Delay(40);
    }

    sws_freeContext(img_convert_ctx);
    SDL_Quit();
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avformat_close_input(&pFormatCtx);
    return 0;
}

int main_single_thread(int argc, const char * argv[])
{
    AVFormatContext *pFormatCtx = NULL;
    AVCodecParameters *pCodecPmts = NULL;
    AVCodec *pCodec = NULL;
    AVFrame *pFrame = NULL;
    AVFrame *pFrameYUV= NULL;
    AVPacket *pPacket;
    uint8_t *out_buffer;
    int i_videoidx;
    int y_size;
    int ret;
    int got_picture;
    struct SwsContext *img_convert_ctx;
    int i;
    
    char filepath[] = "/Users/fengweilun/Desktop/RaceHorses_416x240_30.h265";
    
    int screen_w = 0;
    int screen_h = 0;
    SDL_Window *pSDLWd;
    SDL_Renderer *pSDLRd;
    SDL_Texture *pSDLTx;
    SDL_Rect sSDLRect;
    
    FILE *fp_yuv;
//读取码流，得到码流的信息以及编码格式，确定要使用的编/解码器
    av_register_all();
    avformat_network_init();
    pFormatCtx = avformat_alloc_context();
    if(0 != avformat_open_input(&pFormatCtx, filepath, NULL, NULL))
    {
        printf("open input stream fail\n");
        return -1;
    }
    if(0 > avformat_find_stream_info(pFormatCtx, NULL))
    {
        printf("find error\n");
        return -1;
    }
    i_videoidx = -1;
    for(i = 0; i < pFormatCtx->nb_streams; i++)
        if(pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            i_videoidx = i;
            break;
        }
    if(-1 == i_videoidx)
    {
        printf("Don't know which one is video\n");
        return -1;
    }
    pCodecPmts = pFormatCtx->streams[i_videoidx]->codecpar;
    pCodec = avcodec_find_decoder(pCodecPmts->codec_id);
    if(NULL == pCodec)
    {
        printf("No Codec Found\n");
        return -1;
    }
    if(avcodec_open2(pFormatCtx->streams[i_videoidx]->codec, pCodec, NULL) < 0)
    {
        printf("Cannot Open Codec\n");
        return -1;
    }
    
    //为每一帧分配内存空间
    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();
    out_buffer = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecPmts->width, pCodecPmts->height, 1));
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, pCodecPmts->width, pCodecPmts->height, 1);//利用一个out_buffer来填充pFrameYUV的空间
    
    pPacket = (AVPacket *)av_malloc(sizeof(AVPacket));//给packet分配信息
    printf("------------File Infomation-----------\n");
    av_dump_format(pFormatCtx, 0, filepath, 0);//打印codec信息
    printf("--------------------------------------\n");
    img_convert_ctx = sws_getContext(pCodecPmts->width, pCodecPmts->height, pFormatCtx->streams[i_videoidx]->codec->pix_fmt, pCodecPmts->width, pCodecPmts->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);//转码句柄，确定转码需要的所有工具。
    
    fp_yuv = fopen("output.yuv", "wb+");
    
    //初始化SDL
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        printf("could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    
    screen_w = pCodecPmts->width;
    screen_h = pCodecPmts->height;
    //初始化SDL的Window
    pSDLWd = SDL_CreateWindow("FFmplayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL);
    if(!pSDLWd)
    {
        printf("window init fail\n");
        return -1;
    }
    pSDLRd = SDL_CreateRenderer(pSDLWd, -1, 0);//初始化SDLRender
    pSDLTx = SDL_CreateTexture(pSDLRd, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);//初始化SDLTexture
    sSDLRect.x = 0;
    sSDLRect.y = 0;
    sSDLRect.w = screen_w;
    sSDLRect.h = screen_h;//初始化SDLRectangle
    
    while(av_read_frame(pFormatCtx, pPacket) >= 0)//将一帧信息读入Packet中
    {
        if(pPacket->stream_index == i_videoidx)
        {
            ret = avcodec_decode_video2(pFormatCtx->streams[i_videoidx]->codec, pFrame, &got_picture, pPacket);//将一个packet送入解码，当gotpicture为1的时候说明解码完成1帧，否则没有。
            if(ret < 0)
            {
                printf("Decode Error\n");
                return -1;
            }
            //当有一帧输出的时候
            if(got_picture)
            {
                sws_scale(img_convert_ctx, (const uint8_t * const *)pFrame->data, pFrame->linesize, 0, pCodecPmts->height, pFrameYUV->data, pFrameYUV->linesize);//图像大小以及格式转换。
#ifdef OUTPUT_YUV
                y_size = pCodecPmts->width * pCodecPmts->height;
                fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);
                fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);
                fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);
#endif
                
                SDL_UpdateYUVTexture(pSDLTx, &sSDLRect, pFrameYUV->data[0], pFrameYUV->linesize[0], pFrameYUV->data[1], pFrameYUV->linesize[1], pFrameYUV->data[2], pFrameYUV->linesize[2]);//将YUV上传到Texture
                SDL_RenderClear(pSDLRd);
                SDL_RenderCopy(pSDLRd, pSDLTx, NULL, &sSDLRect);//将Texture利用一个Rectangle利用传给render
                SDL_RenderPresent(pSDLRd);//Render负责显示，传给picture
                SDL_Delay(40);//每当传完，都休息40ms, 如果解码输出一帧时间约等于0，则25fps。现在肯定小于25.
            }
        }
    }
    av_packet_unref(pPacket);
    while(1)//flush剩下的帧，通过unref这个packet实现传入为空，实现flush。
    {
        ret = avcodec_decode_video2(pFormatCtx->streams[i_videoidx]->codec, pFrame, &got_picture, pPacket);
        if(ret < 0 || !got_picture)
            break;

        sws_scale(img_convert_ctx, (const uint8_t * const *)pFrame->data, pFrame->linesize, 0, pCodecPmts->height, pFrameYUV->data, pFrameYUV->linesize);
#ifdef OUTPUT_YUV
        y_size = pCodecPmts->width * pCodecPmts->height;
        fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);
        fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);
        fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);
#endif
        
        SDL_UpdateYUVTexture(pSDLTx, &sSDLRect, pFrameYUV->data[0], pFrameYUV->linesize[0], pFrameYUV->data[1], pFrameYUV->linesize[1], pFrameYUV->data[2], pFrameYUV->linesize[2]);
        SDL_RenderClear(pSDLRd);
        SDL_RenderCopy(pSDLRd, pSDLTx, NULL, &sSDLRect);
        SDL_RenderPresent(pSDLRd);
        SDL_Delay(40);
    }
    sws_freeContext(img_convert_ctx);
    
#if OUTPUT_YUV420P
    fclose(fp_yuv);
#endif
    
    SDL_Quit();
    
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avformat_close_input(&pFormatCtx);
    
    return 0;
}








int main(int argc, const char * argv[])
{
    return main_multi_thread(argc, argv);
}
