#include "videoctl.h"

// 补充
AVDictionary *sws_dict;
AVDictionary *swr_opts;
AVDictionary *format_opts, *codec_opts;

static char error[128];

static void sigterm_handler(int sig)
{
    exit(123);
}

static void uninit_opts(void)
{
    av_dict_free(&swr_opts);
    av_dict_free(&sws_dict);
    av_dict_free(&format_opts);
    av_dict_free(&codec_opts);
}

std::string av_error_string(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

VideoCtl::VideoCtl(QObject *parent) : QObject(parent),
                                      m_initIndex(false),
                                      m_playLoopIndex(false),
                                      m_CurVideoState(nullptr),
                                      m_frameH(0),
                                      m_frameW(0)
{
    Init();
    ConnectSig();
}

VideoCtl::~VideoCtl()
{
    if(m_CurVideoState)
    {
        av_free(m_CurVideoState);
        StopPlay();
    }
}

void VideoCtl::StartPlay(QString strFileName, WId widPlayWid)
{
    int ret = -1;
    int w = 640;
    int h = 480;

    m_playLoopIndex = false;

    std::string stdStrFileName = strFileName.toStdString();
    input_filename = stdStrFileName.c_str();
    emit SigStartPlay(strFileName); // 正式播放，发送给标题栏
    play_wid = widPlayWid;

    if (!display_disable)
    {
        int flags = SDL_WINDOW_HIDDEN;
        if (alwaysontop)
#if SDL_VERSION_ATLEAST(2, 0, 5)
            flags |= SDL_WINDOW_ALWAYS_ON_TOP;
#else
            av_log(NULL, AV_LOG_WARNING, "Your SDL version doesn't support SDL_WINDOW_ALWAYS_ON_TOP. Feature will be inactive.\n");
#endif
        if (borderless)
            flags |= SDL_WINDOW_BORDERLESS;
        else
            flags |= SDL_WINDOW_RESIZABLE;

#ifdef SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR
        SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
        if (hwaccel && !enable_vulkan)
        {
            av_log(NULL, AV_LOG_INFO, "Enable vulkan renderer to support hwaccel %s\n", hwaccel);
            enable_vulkan = 1;
        }
        if (enable_vulkan)
        {
            vk_renderer = vk_get_renderer();
            if (vk_renderer)
            {
#if SDL_VERSION_ATLEAST(2, 0, 6)
                flags |= SDL_WINDOW_VULKAN;
#endif
            }
            else
            {
                av_log(NULL, AV_LOG_WARNING, "Doesn't support vulkan renderer, fallback to SDL renderer\n");
                enable_vulkan = 0;
            }
        }

        window = SDL_CreateWindowFrom((void *)play_wid); // 通过native window创建窗口
        SDL_GetWindowSize(window, &w, &h);               // 初始宽高设置为显示控件宽高
        // window = SDL_CreateWindow(input_filename, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, default_width, default_height, flags);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        if (!window)
        {
            av_log(NULL, AV_LOG_FATAL, "Failed to create window: %s", SDL_GetError());
            do_exit();
        }

        if (vk_renderer)
        {
            AVDictionary *dict = NULL;

            if (vulkan_params)
                av_dict_parse_string(&dict, vulkan_params, "=", ":", 0);
            ret = vk_renderer_create(vk_renderer, window, dict);
            av_dict_free(&dict);
            if (ret < 0)
            {
                av_log(NULL, AV_LOG_FATAL, "Failed to create vulkan renderer, %s\n", av_error_string(ret));
                do_exit();
            }
        }
        else
        {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!renderer)
            {
                av_log(NULL, AV_LOG_WARNING, "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
                renderer = SDL_CreateRenderer(window, -1, 0);
            }
            if (renderer)
            {
                if (!SDL_GetRendererInfo(renderer, &renderer_info))
                    av_log(NULL, AV_LOG_VERBOSE, "Initialized %s renderer.\n", renderer_info.name);
            }
            if (!renderer || !renderer_info.num_texture_formats)
            {
                av_log(NULL, AV_LOG_FATAL, "Failed to create window or renderer: %s", SDL_GetError());
                do_exit();
            }
        }
    }

    // 打开音频流和视频流
    stream_open(input_filename, file_iformat);
    if (!m_CurVideoState)
    {
        av_log(nullptr, AV_LOG_ERROR, "Failed to initialize AVState\n");
        do_exit();
    }

    m_CurVideoState->width = w;
    m_CurVideoState->height = h;

    // SDL事件循环处理
    event_loop();
}

void VideoCtl::StopPlay()
{
    do_exit();
    exit(999);
}

int VideoCtl::audio_open(void *opaque, AVChannelLayout *wanted_channel_layout, int wanted_sample_rate, AudioParams *audio_hw_params)
{
    SDL_AudioSpec wanted_spec, spec;
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;
    int wanted_nb_channels = wanted_channel_layout->nb_channels;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        wanted_nb_channels = atoi(env);
        av_channel_layout_uninit(wanted_channel_layout);
        av_channel_layout_default(wanted_channel_layout, wanted_nb_channels);
    }
    if (wanted_channel_layout->order != AV_CHANNEL_ORDER_NATIVE) {
        av_channel_layout_uninit(wanted_channel_layout);
        av_channel_layout_default(wanted_channel_layout, wanted_nb_channels);
    }
    wanted_nb_channels = wanted_channel_layout->nb_channels;
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        av_log(NULL, AV_LOG_ERROR, "Invalid sample rate or channel count!\n");
        return -1;
    }
    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = opaque;
    while (!(audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE))) {
        av_log(NULL, AV_LOG_WARNING, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                av_log(NULL, AV_LOG_ERROR,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        av_channel_layout_default(wanted_channel_layout, wanted_spec.channels);
    }
    if (spec.format != AUDIO_S16SYS) {
        av_log(NULL, AV_LOG_ERROR,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        av_channel_layout_uninit(wanted_channel_layout);
        av_channel_layout_default(wanted_channel_layout, spec.channels);
        if (wanted_channel_layout->order != AV_CHANNEL_ORDER_NATIVE) {
            av_log(NULL, AV_LOG_ERROR,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    if (av_channel_layout_copy(&audio_hw_params->ch_layout, wanted_channel_layout) < 0)
        return -1;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->ch_layout.nb_channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->ch_layout.nb_channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    return spec.size;
}

bool VideoCtl::Init()
{
    if (m_initIndex)
        return true;

    ConnectSig();

    int flags, ret;

#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
    avformat_network_init();

    signal(SIGINT, sigterm_handler);  /* Interrupt (ANSI).    */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */

    av_log_set_level(AV_LOG_PANIC);

    if (display_disable)
    {
        video_disable = 1;
    }

    flags = SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER;
    if (audio_disable)
        flags &= ~SDL_INIT_AUDIO;
    else
    {
        /* Try to work around an occasional ALSA buffer underflow issue when the
         * period size is NPOT due to ALSA resampling by forcing the buffer size. */
        if (!SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
            SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", "1", 1);
    }
    if (display_disable)
        flags &= ~SDL_INIT_VIDEO;
    if (SDL_Init(flags))
    {
        av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
        av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
        exit(1);
    }

    SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
    SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

    m_initIndex = true;

    return true;
}

void VideoCtl::ConnectSig()
{
    
}

void VideoCtl::stream_open(const char *filename, const AVInputFormat *iformat)
{
    if (m_CurVideoState != nullptr)
        return;
    m_CurVideoState = new VideoState();
    m_CurVideoState->last_video_stream = m_CurVideoState->video_stream = -1;
    m_CurVideoState->last_audio_stream = m_CurVideoState->audio_stream = -1;
    m_CurVideoState->last_subtitle_stream = m_CurVideoState->subtitle_stream = -1;
    m_CurVideoState->filename = av_strdup(filename);
    if (!m_CurVideoState->filename)
        goto fail;
    m_CurVideoState->iformat = iformat;
    m_CurVideoState->ytop = 0;
    m_CurVideoState->xleft = 0;

    /* start video dm_CurVideoStateplay */
    if (frame_queue_init(&m_CurVideoState->video.pictq, &m_CurVideoState->video.videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
        goto fail;
    if (frame_queue_init(&m_CurVideoState->subtitle.subpq, &m_CurVideoState->subtitle.subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
        goto fail;
    if (frame_queue_init(&m_CurVideoState->audio.sampq, &m_CurVideoState->audio.audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
        goto fail;

    if (packet_queue_init(&m_CurVideoState->video.videoq) < 0 ||
        packet_queue_init(&m_CurVideoState->audio.audioq) < 0 ||
        packet_queue_init(&m_CurVideoState->subtitle.subtitleq) < 0)
        goto fail;

    if (!(m_CurVideoState->continue_read_thread = SDL_CreateCond()))
    {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        goto fail;
    }

    init_clock(&m_CurVideoState->vidclk, &m_CurVideoState->video.videoq.serial);
    init_clock(&m_CurVideoState->audclk, &m_CurVideoState->audio.audioq.serial);
    init_clock(&m_CurVideoState->extclk, &m_CurVideoState->extclk.serial);
    m_CurVideoState->audio_clock_serial = -1;
    if (startup_volume < 0)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d < 0, setting to 0\n", startup_volume);
    if (startup_volume > 100)
        av_log(NULL, AV_LOG_WARNING, "-volume=%d > 100, setting to 100\n", startup_volume);
    startup_volume = av_clip(startup_volume, 0, 100);
    startup_volume = av_clip(SDL_MIX_MAXVOLUME * startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
    m_CurVideoState->audio.audio_volume = startup_volume;
    m_CurVideoState->audio.muted = 0;
    m_CurVideoState->av_sync_type = av_sync_type;
    m_CurVideoState->read_tid = SDL_CreateThread(read_thread, "read_thread", m_CurVideoState);
    if (!m_CurVideoState->read_tid)
    {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateThread(): %s\n", SDL_GetError());
    fail:
        stream_close();
        return;
    }
}

void VideoCtl::event_loop()
{
    SDL_Event event;
    double incr, pos, frac;

    for (;;)
    {
        double x;
        refresh_loop_wait_event(&event);
        switch (event.type)
        {
        case SDL_MOUSEBUTTONDOWN:
            if (exit_on_mousedown)
            {
                do_exit();
                break;
            }
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                static int64_t last_mouse_left_click = 0;
                if (av_gettime_relative() - last_mouse_left_click <= 500000)
                {
                    toggle_full_screen();
                    m_CurVideoState->force_refresh = 1;
                    last_mouse_left_click = 0;
                }
                else
                {
                    last_mouse_left_click = av_gettime_relative();
                }
            }
        case SDL_MOUSEMOTION:
            if (cursor_hidden)
            {
                SDL_ShowCursor(1);
                cursor_hidden = 0;
            }
            cursor_last_shown = av_gettime_relative();
            if (event.type == SDL_MOUSEBUTTONDOWN)
            {
                if (event.button.button != SDL_BUTTON_RIGHT)
                    break;
                x = event.button.x;
            }
            else
            {
                if (!(event.motion.state & SDL_BUTTON_RMASK))
                    break;
                x = event.motion.x;
            }
            if (seek_by_bytes || m_CurVideoState->ic->duration <= 0)
            {
                uint64_t size = avio_size(m_CurVideoState->ic->pb);
                stream_seek(m_CurVideoState, size * x / m_CurVideoState->width, 0, 1);
            }
            else
            {
                int64_t ts;
                int ns, hh, mm, ss;
                int tns, thh, tmm, tss;
                tns = m_CurVideoState->ic->duration / 1000000LL;
                thh = tns / 3600;
                tmm = (tns % 3600) / 60;
                tss = (tns % 60);
                frac = x / m_CurVideoState->width;
                ns = frac * tns;
                hh = ns / 3600;
                mm = (ns % 3600) / 60;
                ss = (ns % 60);
                av_log(NULL, AV_LOG_INFO,
                       "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac * 100,
                       hh, mm, ss, thh, tmm, tss);
                ts = frac * m_CurVideoState->ic->duration;
                if (m_CurVideoState->ic->start_time != AV_NOPTS_VALUE)
                    ts += m_CurVideoState->ic->start_time;
                stream_seek(m_CurVideoState, ts, 0, 0);
            }
            break;
        case SDL_WINDOWEVENT:
            switch (event.window.event)
            {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                screen_width = m_CurVideoState->width = event.window.data1;
                screen_height = m_CurVideoState->height = event.window.data2;
                if (m_CurVideoState->vis.vis_texture)
                {
                    SDL_DestroyTexture(m_CurVideoState->vis.vis_texture);
                    m_CurVideoState->vis.vis_texture = NULL;
                }
                if (vk_renderer)
                    vk_renderer_resize(vk_renderer, screen_width, screen_height);
            case SDL_WINDOWEVENT_EXPOSED:
                m_CurVideoState->force_refresh = 1;
            }
            break;
        case SDL_QUIT:
        case FF_QUIT_EVENT:
            do_exit();
            break;
        default:
            break;
        }
    }
}

void VideoCtl::do_exit()
{
    if (m_CurVideoState)
    {
        stream_close();
    }
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (vk_renderer)
        vk_renderer_destroy(vk_renderer);
    if (window)
        SDL_DestroyWindow(window);
    uninit_opts();
    for (int i = 0; i < nb_vfilters; i++)
        av_freep(&vfilters_list[i]);
    av_freep(&vfilters_list);
    av_freep(&video_codec_name);
    av_freep(&audio_codec_name);
    av_freep(&subtitle_codec_name);
    if (input_filename)
        input_filename = nullptr;
    avformat_network_deinit();
    if (show_status)
        printf("\n");
    SDL_Quit();
    av_log(NULL, AV_LOG_QUIET, "%s", "");
    //exit(0);
}

void VideoCtl::refresh_loop_wait_event(SDL_Event *event)
{
    double remaining_time = 0.0;
    SDL_PumpEvents();
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT))
    {
        if (!cursor_hidden && av_gettime_relative() - cursor_last_shown > CURSOR_HIDE_DELAY)
        {
            SDL_ShowCursor(0);
            cursor_hidden = 1;
        }
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        if (m_CurVideoState->show_mode != VideoState::ShowMode::SHOW_MODE_NONE && (!m_CurVideoState->paused || m_CurVideoState->force_refresh))
            video_refresh(&remaining_time);
        SDL_PumpEvents();
    }
}

void VideoCtl::stream_close()
{
    /* XXX: use a special url_shutdown call to abort parse cleanly */
    m_CurVideoState->abort_request = 1;
    SDL_WaitThread(m_CurVideoState->read_tid, NULL);

    /* close each stream */
    if (m_CurVideoState->audio_stream >= 0)
        stream_component_close(m_CurVideoState->audio_stream);
    if (m_CurVideoState->video_stream >= 0)
        stream_component_close(m_CurVideoState->video_stream);
    if (m_CurVideoState->subtitle_stream >= 0)
        stream_component_close(m_CurVideoState->subtitle_stream);

    avformat_close_input(&m_CurVideoState->ic);

    packet_queue_destroy(&m_CurVideoState->video.videoq);
    packet_queue_destroy(&m_CurVideoState->audio.audioq);
    packet_queue_destroy(&m_CurVideoState->subtitle.subtitleq);

    /* free all pictures */
    frame_queue_destroy(&m_CurVideoState->video.pictq);
    frame_queue_destroy(&m_CurVideoState->audio.sampq);
    frame_queue_destroy(&m_CurVideoState->subtitle.subpq);
    SDL_DestroyCond(m_CurVideoState->continue_read_thread);
    sws_freeContext(m_CurVideoState->video.sub_convert_ctx);
    av_free(m_CurVideoState->filename);
    if (m_CurVideoState->vis.vis_texture)
        SDL_DestroyTexture(m_CurVideoState->vis.vis_texture);
    if (m_CurVideoState->video.vid_texture)
        SDL_DestroyTexture(m_CurVideoState->video.vid_texture);
    if (m_CurVideoState->sub_texture)
        SDL_DestroyTexture(m_CurVideoState->sub_texture);
    //av_free(m_CurVideoState);
}

void VideoCtl::stream_component_close(int stream_index)
{
    AVFormatContext *ic = m_CurVideoState->ic;
    AVCodecParameters *codecpar;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return;
    codecpar = ic->streams[stream_index]->codecpar;

    switch (codecpar->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
    {
        decoder_abort(&m_CurVideoState->audio.auddec, &m_CurVideoState->audio.sampq);
        SDL_CloseAudioDevice(audio_dev);
        decoder_destroy(&m_CurVideoState->audio.auddec);
        swr_free(&m_CurVideoState->audio.swr_ctx);
        av_freep(&m_CurVideoState->audio.audio_buf1);
        m_CurVideoState->audio.audio_buf1_size = 0;
        m_CurVideoState->audio.audio_buf = NULL;

        if (m_CurVideoState->vis.rdft)
        {
            av_tx_uninit(&m_CurVideoState->vis.rdft);
            av_freep(&m_CurVideoState->vis.real_data);
            av_freep(&m_CurVideoState->vis.rdft_data);
            m_CurVideoState->vis.rdft = NULL;
            m_CurVideoState->vis.rdft_bits = 0;
        }
        break;
    }

    case AVMEDIA_TYPE_VIDEO:
    {
        decoder_abort(&m_CurVideoState->video.viddec, &m_CurVideoState->video.pictq);
        decoder_destroy(&m_CurVideoState->video.viddec);
        break;
    }

    case AVMEDIA_TYPE_SUBTITLE:
    {
        decoder_abort(&m_CurVideoState->subtitle.subdec, &m_CurVideoState->subtitle.subpq);
        decoder_destroy(&m_CurVideoState->subtitle.subdec);
        break;
    }

    default:
        break;
    }

    ic->streams[stream_index]->discard = AVDISCARD_ALL;
    switch (codecpar->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        m_CurVideoState->audio.audio_st = NULL;
        m_CurVideoState->audio_stream = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        m_CurVideoState->video.video_st = NULL;
        m_CurVideoState->video_stream = -1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        m_CurVideoState->subtitle.subtitle_st = NULL;
        m_CurVideoState->subtitle_stream = -1;
        break;
    default:
        break;
    }
}

int VideoCtl::decoder_start(Decoder *d, int (*fn)(void *), const char *thread_name, void *arg)
{
    packet_queue_start(d->queue);
    d->decode_thread = SDL_CreateThread(fn, thread_name, arg);
    if (!d->decode_thread)
    {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateThread(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}

int VideoCtl::video_open()
{
    int w, h;

    w = screen_width ? screen_width : default_width;
    h = screen_height ? screen_height : default_height;

    if (!window_title)
        window_title = input_filename;
    SDL_SetWindowTitle(window, window_title);

    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, screen_left, screen_top);
    if (is_full_screen)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_ShowWindow(window);

    m_CurVideoState->width = w;
    m_CurVideoState->height = h;

    return 0;
}

void VideoCtl::sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{
    VideoState *is = reinterpret_cast<VideoState *>(opaque);
    int audio_size, len1;

    audio_callback_time = av_gettime_relative();

    while (len > 0)
    {
        if (is->audio.audio_buf_index >= is->audio.audio_buf_size)
        {
            audio_size = audio_decode_frame(is);
            if (audio_size < 0)
            {
                /* if error, just output silence */
                is->audio.audio_buf = NULL;
                is->audio.audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio.audio_tgt.frame_size * is->audio.audio_tgt.frame_size;
            }
            else
            {
                if (is->show_mode != VideoState::ShowMode::SHOW_MODE_VIDEO)
                    VideoCtl::update_sample_display(is, (int16_t *)is->audio.audio_buf, audio_size);
                is->audio.audio_buf_size = audio_size;
            }
            is->audio.audio_buf_index = 0;
        }
        len1 = is->audio.audio_buf_size - is->audio.audio_buf_index;
        if (len1 > len)
            len1 = len;
        if (!is->audio.muted && is->audio.audio_buf && is->audio.audio_volume == SDL_MIX_MAXVOLUME)
            memcpy(stream, (uint8_t *)is->audio.audio_buf + is->audio.audio_buf_index, len1);
        else
        {
            memset(stream, 0, len1);
            if (!is->audio.muted && is->audio.audio_buf)
                SDL_MixAudioFormat(stream, (uint8_t *)is->audio.audio_buf + is->audio.audio_buf_index, AUDIO_S16SYS, len1, is->audio.audio_volume);
        }
        len -= len1;
        stream += len1;
        is->audio.audio_buf_index += len1;
    }
    is->audio.audio_write_buf_size = is->audio.audio_buf_size - is->audio.audio_buf_index;
    /* Let's assume the audio driver that is used by SDL has two periods. */
    if (!isnan(is->audio_clock))
    {
        VideoCtl::set_clock_at(&is->audclk, is->audio_clock - (double)(2 * is->audio.audio_hw_buf_size + is->audio.audio_write_buf_size) / is->audio.audio_tgt.bytes_per_sec, is->audio_clock_serial, audio_callback_time / 1000000.0);
        VideoCtl::sync_clock_to_slave(&is->extclk, &is->audclk);
    }
}

void VideoCtl::seek_chapter(int incr)
{
    int64_t pos = get_master_clock(m_CurVideoState) * AV_TIME_BASE;
    int i;

    if (!m_CurVideoState->ic->nb_chapters)
        return;

    /* find the current chapter */
    for (i = 0; i < m_CurVideoState->ic->nb_chapters; i++)
    {
        AVChapter *ch = m_CurVideoState->ic->chapters[i];
        if (av_compare_ts(pos, AV_TIME_BASE_Q, ch->start, ch->time_base) < 0)
        {
            i--;
            break;
        }
    }

    i += incr;
    i = FFMAX(i, 0);
    if (i >= m_CurVideoState->ic->nb_chapters)
        return;

    av_log(NULL, AV_LOG_VERBOSE, "Seeking to chapter %d.\n", i);
    stream_seek(m_CurVideoState, av_rescale_q(m_CurVideoState->ic->chapters[i]->start, m_CurVideoState->ic->chapters[i]->time_base, AV_TIME_BASE_Q),
                0, 0);
}

void VideoCtl::toggle_full_screen()
{
    is_full_screen = !is_full_screen;
    SDL_SetWindowFullscreen(window, is_full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void VideoCtl::toggle_audio_display()
{
    int next = m_CurVideoState->show_mode;
    do
    {
        next = (next + 1) % VideoState::ShowMode::SHOW_MODE_NB;
    } while (next != m_CurVideoState->show_mode && (next == VideoState::ShowMode::SHOW_MODE_VIDEO && !m_CurVideoState->video.video_st || next != VideoState::ShowMode::SHOW_MODE_VIDEO && !m_CurVideoState->audio.audio_st));
    if (m_CurVideoState->show_mode != next)
    {
        m_CurVideoState->force_refresh = 1;
        m_CurVideoState->show_mode = static_cast<VideoState::ShowMode>(next);
    }
}

void VideoCtl::stream_cycle_channel(int codec_type)
{
    AVFormatContext *ic = m_CurVideoState->ic;
    int start_index, stream_index;
    int old_index;
    AVStream *st;
    AVProgram *p = NULL;
    int nb_streams = m_CurVideoState->ic->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO)
    {
        start_index = m_CurVideoState->last_video_stream;
        old_index = m_CurVideoState->video_stream;
    }
    else if (codec_type == AVMEDIA_TYPE_AUDIO)
    {
        start_index = m_CurVideoState->last_audio_stream;
        old_index = m_CurVideoState->audio_stream;
    }
    else
    {
        start_index = m_CurVideoState->last_subtitle_stream;
        old_index = m_CurVideoState->subtitle_stream;
    }
    stream_index = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && m_CurVideoState->video_stream != -1)
    {
        p = av_find_program_from_stream(ic, NULL, m_CurVideoState->video_stream);
        if (p)
        {
            nb_streams = p->nb_stream_indexes;
            for (start_index = 0; start_index < nb_streams; start_index++)
                if (p->stream_index[start_index] == stream_index)
                    break;
            if (start_index == nb_streams)
                start_index = -1;
            stream_index = start_index;
        }
    }

    for (;;)
    {
        if (++stream_index >= nb_streams)
        {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                stream_index = -1;
                m_CurVideoState->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = m_CurVideoState->ic->streams[p ? p->stream_index[stream_index] : stream_index];
        if (st->codecpar->codec_type == codec_type)
        {
            /* check that parameters are OK */
            switch (codec_type)
            {
            case AVMEDIA_TYPE_AUDIO:
                if (st->codecpar->sample_rate != 0 &&
                    st->codecpar->ch_layout.nb_channels != 0)
                    goto the_end;
                break;
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_SUBTITLE:
                goto the_end;
            default:
                break;
            }
        }
    }
the_end:
    if (p && stream_index != -1)
        stream_index = p->stream_index[stream_index];
    av_log(NULL, AV_LOG_INFO, "Switch %s stream from #%d to #%d\n",
           av_get_media_type_string(static_cast<AVMediaType>(codec_type)),
           old_index,
           stream_index);

    stream_component_close(old_index);
    stream_component_open(m_CurVideoState, stream_index);
}

void VideoCtl::init_clock(Clock *c, int *queue_serial)
{
    c->speed = 1.0;
    c->paused = 0;
    c->queue_serial = queue_serial;
    set_clock(c, NAN, -1);
}

void VideoCtl::set_clock(Clock *c, double pts, int serial)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, serial, time);
}

void VideoCtl::set_clock_at(Clock *c, double pts, int serial, double time)
{
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
}

double VideoCtl::get_clock(Clock *c)
{
    if (*c->queue_serial != c->serial)
        return NAN;
    if (c->paused)
    {
        return c->pts;
    }
    else
    {
        double time = av_gettime_relative() / 1000000.0;
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

void VideoCtl::sync_clock_to_slave(Clock *c, Clock *slave)
{
    double clock = get_clock(c);
    double slave_clock = get_clock(slave);
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(c, slave_clock, slave->serial);
}

void VideoCtl::update_volume(int sign, double step)
{
    double volume_level = m_CurVideoState->audio.audio_volume ? (20 * log(m_CurVideoState->audio.audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    m_CurVideoState->audio.audio_volume = av_clip(m_CurVideoState->audio.audio_volume == new_volume ? (m_CurVideoState->audio.audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}

void VideoCtl::update_video_pts(double pts, int serial)
{
    set_clock(&m_CurVideoState->vidclk, pts, serial);
    sync_clock_to_slave(&m_CurVideoState->extclk, &m_CurVideoState->vidclk);
}

double VideoCtl::compute_target_delay(double delay)
{
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (get_master_sync_type(m_CurVideoState) != AV_SYNC_VIDEO_MASTER)
    {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        diff = get_clock(&m_CurVideoState->vidclk) - get_master_clock(m_CurVideoState);

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < m_CurVideoState->max_frame_duration)
        {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
           delay, -diff);

    return delay;
}

double VideoCtl::vp_duration(Frame *vp, Frame *nextvp)
{
    if (vp->serial == nextvp->serial)
    {
        double duration = nextvp->pts - vp->pts;
        if (isnan(duration) || duration <= 0 || duration > m_CurVideoState->max_frame_duration)
            return vp->duration;
        else
            return duration;
    }
    else
    {
        return 0.0;
    }
}

void VideoCtl::check_external_clock_speed()
{
    if (m_CurVideoState->video_stream >= 0 && m_CurVideoState->video.videoq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES ||
        m_CurVideoState->audio_stream >= 0 && m_CurVideoState->audio.audioq.nb_packets <= EXTERNAL_CLOCK_MIN_FRAMES)
    {
        set_clock_speed(&m_CurVideoState->extclk, FFMAX(EXTERNAL_CLOCK_SPEED_MIN, m_CurVideoState->extclk.speed - EXTERNAL_CLOCK_SPEED_STEP));
    }
    else if ((m_CurVideoState->video_stream < 0 || m_CurVideoState->video.videoq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES) &&
             (m_CurVideoState->audio_stream < 0 || m_CurVideoState->audio.audioq.nb_packets > EXTERNAL_CLOCK_MAX_FRAMES))
    {
        set_clock_speed(&m_CurVideoState->extclk, FFMIN(EXTERNAL_CLOCK_SPEED_MAX, m_CurVideoState->extclk.speed + EXTERNAL_CLOCK_SPEED_STEP));
    }
    else
    {
        double speed = m_CurVideoState->extclk.speed;
        if (speed != 1.0)
            set_clock_speed(&m_CurVideoState->extclk, speed + EXTERNAL_CLOCK_SPEED_STEP * (1.0 - speed) / fabs(1.0 - speed));
    }
}

void VideoCtl::set_clock_speed(Clock *c, double speed)
{
    set_clock(c, get_clock(c), c->serial);
    c->speed = speed;
}

void VideoCtl::video_refresh(double *remaining_time)
{
    double time;

    Frame *sp, *sp2;

    if (!m_CurVideoState->paused && get_master_sync_type(m_CurVideoState) == AV_SYNC_EXTERNAL_CLOCK && m_CurVideoState->realtime)
        check_external_clock_speed();

    if (!display_disable && m_CurVideoState->show_mode != VideoState::ShowMode::SHOW_MODE_VIDEO && m_CurVideoState->audio.audio_st)
    {
        time = av_gettime_relative() / 1000000.0;
        if (m_CurVideoState->force_refresh || m_CurVideoState->vis.last_vis_time + rdftspeed < time)
        {
            video_display();
            m_CurVideoState->vis.last_vis_time = time;
        }
        *remaining_time = FFMIN(*remaining_time, m_CurVideoState->vis.last_vis_time + rdftspeed - time);
    }

    if (m_CurVideoState->video.video_st)
    {
    retry:
        if (frame_queue_nb_remaining(&m_CurVideoState->video.pictq) == 0)
        {
            // nothing to do, no picture to display in the queue
        }
        else
        {
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            lastvp = frame_queue_peek_last(&m_CurVideoState->video.pictq);
            vp = frame_queue_peek(&m_CurVideoState->video.pictq);

            if (vp->serial != m_CurVideoState->video.videoq.serial)
            {
                frame_queue_next(&m_CurVideoState->video.pictq);
                goto retry;
            }

            if (lastvp->serial != vp->serial)
                m_CurVideoState->video.frame_timer = av_gettime_relative() / 1000000.0;

            if (m_CurVideoState->paused)
                goto display;

            /* compute nominal last_duration */
            last_duration = vp_duration(lastvp, vp);
            delay = compute_target_delay(last_duration);

            time = av_gettime_relative() / 1000000.0;
            if (time < m_CurVideoState->video.frame_timer + delay)
            {
                *remaining_time = FFMIN(m_CurVideoState->video.frame_timer + delay - time, *remaining_time);
                goto display;
            }

            m_CurVideoState->video.frame_timer += delay;
            if (delay > 0 && time - m_CurVideoState->video.frame_timer > AV_SYNC_THRESHOLD_MAX)
                m_CurVideoState->video.frame_timer = time;

            SDL_LockMutex(m_CurVideoState->video.pictq.mutex);
            if (!isnan(vp->pts))
                update_video_pts(vp->pts, vp->serial);
            SDL_UnlockMutex(m_CurVideoState->video.pictq.mutex);

            if (frame_queue_nb_remaining(&m_CurVideoState->video.pictq) > 1)
            {
                Frame *nextvp = frame_queue_peek_next(&m_CurVideoState->video.pictq);
                duration = vp_duration(vp, nextvp);
                if (!m_CurVideoState->step && (framedrop > 0 || (framedrop && get_master_sync_type(m_CurVideoState) != AV_SYNC_VIDEO_MASTER)) && time > m_CurVideoState->video.frame_timer + duration)
                {
                    m_CurVideoState->frame_drops_late++;
                    frame_queue_next(&m_CurVideoState->video.pictq);
                    goto retry;
                }
            }

            if (m_CurVideoState->subtitle.subtitle_st)
            {
                while (frame_queue_nb_remaining(&m_CurVideoState->subtitle.subpq) > 0)
                {
                    sp = frame_queue_peek(&m_CurVideoState->subtitle.subpq);

                    if (frame_queue_nb_remaining(&m_CurVideoState->subtitle.subpq) > 1)
                        sp2 = frame_queue_peek_next(&m_CurVideoState->subtitle.subpq);
                    else
                        sp2 = NULL;

                    if (sp->serial != m_CurVideoState->subtitle.subtitleq.serial || (m_CurVideoState->vidclk.pts > (sp->pts + ((float)sp->sub.end_display_time / 1000))) || (sp2 && m_CurVideoState->vidclk.pts > (sp2->pts + ((float)sp2->sub.start_display_time / 1000))))
                    {
                        if (sp->uploaded)
                        {
                            int i;
                            for (i = 0; i < sp->sub.num_rects; i++)
                            {
                                AVSubtitleRect *sub_rect = sp->sub.rects[i];
                                uint8_t *pixels;
                                int pitch, j;

                                if (!SDL_LockTexture(m_CurVideoState->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch))
                                {
                                    for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                        memset(pixels, 0, sub_rect->w << 2);
                                    SDL_UnlockTexture(m_CurVideoState->sub_texture);
                                }
                            }
                        }
                        frame_queue_next(&m_CurVideoState->subtitle.subpq);
                    }
                    else
                    {
                        break;
                    }
                }
            }

            frame_queue_next(&m_CurVideoState->video.pictq);
            m_CurVideoState->force_refresh = 1;

            if (m_CurVideoState->step && !m_CurVideoState->paused)
                stream_toggle_pause(m_CurVideoState);
        }
    display:
        /* display picture */
        if (!display_disable && m_CurVideoState->force_refresh && m_CurVideoState->show_mode == VideoState::ShowMode::SHOW_MODE_VIDEO && m_CurVideoState->video.pictq.rindex_shown)
            video_display();
    }
    m_CurVideoState->force_refresh = 0;
    if (show_status)
    {
        AVBPrint buf;
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize;
        double av_diff;

        cur_time = av_gettime_relative();
        if (!last_time || (cur_time - last_time) >= 30000)
        {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (m_CurVideoState->audio.audio_st)
                aqsize = m_CurVideoState->audio.audioq.size;
            if (m_CurVideoState->video.video_st)
                vqsize = m_CurVideoState->video.videoq.size;
            if (m_CurVideoState->subtitle.subtitle_st)
                sqsize = m_CurVideoState->subtitle.subtitleq.size;
            av_diff = 0;
            if (m_CurVideoState->audio.audio_st && m_CurVideoState->video.video_st)
                av_diff = get_clock(&m_CurVideoState->audclk) - get_clock(&m_CurVideoState->vidclk);
            else if (m_CurVideoState->video.video_st)
                av_diff = get_master_clock(m_CurVideoState) - get_clock(&m_CurVideoState->vidclk);
            else if (m_CurVideoState->audio.audio_st)
                av_diff = get_master_clock(m_CurVideoState) - get_clock(&m_CurVideoState->audclk);

            av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
            av_bprintf(&buf,
                       "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB \r",
                       get_master_clock(m_CurVideoState),
                       (m_CurVideoState->audio.audio_st && m_CurVideoState->video.video_st) ? "A-V" : (m_CurVideoState->video.video_st ? "M-V" : (m_CurVideoState->audio.audio_st ? "M-A" : "   ")),
                       av_diff,
                       m_CurVideoState->video.frame_drops_early + m_CurVideoState->video.frame_drops_late,
                       aqsize / 1024,
                       vqsize / 1024,
                       sqsize);

            if (show_status == 1 && AV_LOG_INFO > av_log_get_level())
                fprintf(stderr, "%s", buf.str);
            else
                av_log(NULL, AV_LOG_INFO, "%s", buf.str);

            fflush(stderr);
            av_bprint_finalize(&buf, NULL);

            last_time = cur_time;
        }
    }
}

void VideoCtl::video_display()
{
    if (!m_CurVideoState->width)
        video_open();

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (m_CurVideoState->audio.audio_st && m_CurVideoState->show_mode != VideoState::ShowMode::SHOW_MODE_VIDEO)
        video_audio_display();
    else if (m_CurVideoState->video.video_st)
        video_image_display();
    SDL_RenderPresent(renderer);
}

void VideoCtl::video_audio_display()
{
    int i, i_start, x, y1, y, ys, delay, n, nb_display_channels;
    int ch, channels, h, h2;
    int64_t time_diff;
    int rdft_bits, nb_freq;

    for (rdft_bits = 1; (1 << rdft_bits) < 2 * m_CurVideoState->height; rdft_bits++)
        ;
    nb_freq = 1 << (rdft_bits - 1);

    /* compute display index : center on currently output samples */
    channels = m_CurVideoState->audio.audio_tgt.ch_layout.nb_channels;
    nb_display_channels = channels;
    if (!m_CurVideoState->paused)
    {
        int data_used = m_CurVideoState->show_mode == VideoState::ShowMode::SHOW_MODE_WAVES ? m_CurVideoState->width : (2 * nb_freq);
        n = 2 * channels;
        delay = m_CurVideoState->audio.audio_write_buf_size;
        delay /= n;

        /* to be more precise, we take into account the time spent since
           the last buffer computation */
        if (audio_callback_time)
        {
            time_diff = av_gettime_relative() - audio_callback_time;
            delay -= (time_diff * m_CurVideoState->audio.audio_tgt.freq) / 1000000;
        }

        delay += 2 * data_used;
        if (delay < data_used)
            delay = data_used;

        i_start = x = compute_mod(m_CurVideoState->vis.sample_array_index - delay * channels, SAMPLE_ARRAY_SIZE);
        if (m_CurVideoState->show_mode == VideoState::ShowMode::SHOW_MODE_WAVES)
        {
            h = INT_MIN;
            for (i = 0; i < 1000; i += channels)
            {
                int idx = (SAMPLE_ARRAY_SIZE + x - i) % SAMPLE_ARRAY_SIZE;
                int a = m_CurVideoState->vis.sample_array[idx];
                int b = m_CurVideoState->vis.sample_array[(idx + 4 * channels) % SAMPLE_ARRAY_SIZE];
                int c = m_CurVideoState->vis.sample_array[(idx + 5 * channels) % SAMPLE_ARRAY_SIZE];
                int d = m_CurVideoState->vis.sample_array[(idx + 9 * channels) % SAMPLE_ARRAY_SIZE];
                int score = a - d;
                if (h < score && (b ^ c) < 0)
                {
                    h = score;
                    i_start = idx;
                }
            }
        }

        m_CurVideoState->vis.last_i_start = i_start;
    }
    else
    {
        i_start = m_CurVideoState->vis.last_i_start;
    }

    if (m_CurVideoState->show_mode == VideoState::ShowMode::SHOW_MODE_WAVES)
    {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        /* total height for one channel */
        h = m_CurVideoState->height / nb_display_channels;
        /* graph height / 2 */
        h2 = (h * 9) / 20;
        for (ch = 0; ch < nb_display_channels; ch++)
        {
            i = i_start + ch;
            y1 = m_CurVideoState->ytop + ch * h + (h / 2); /* position of center line */
            for (x = 0; x < m_CurVideoState->width; x++)
            {
                y = (m_CurVideoState->vis.sample_array[i] * h2) >> 15;
                if (y < 0)
                {
                    y = -y;
                    ys = y1 - y;
                }
                else
                {
                    ys = y1;
                }
                fill_rectangle(m_CurVideoState->xleft + x, ys, 1, y);
                i += channels;
                if (i >= SAMPLE_ARRAY_SIZE)
                    i -= SAMPLE_ARRAY_SIZE;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);

        for (ch = 1; ch < nb_display_channels; ch++)
        {
            y = m_CurVideoState->ytop + ch * h;
            fill_rectangle(m_CurVideoState->xleft, y, m_CurVideoState->width, 1);
        }
    }
    else
    {
        int err = 0;
        if (realloc_texture(&m_CurVideoState->vis.vis_texture, SDL_PIXELFORMAT_ARGB8888, m_CurVideoState->width, m_CurVideoState->height, SDL_BLENDMODE_NONE, 1) < 0)
            return;

        if (m_CurVideoState->vis.xpos >= m_CurVideoState->width)
            m_CurVideoState->vis.xpos = 0;
        nb_display_channels = FFMIN(nb_display_channels, 2);
        if (rdft_bits != m_CurVideoState->vis.rdft_bits)
        {
            const float rdft_scale = 1.0;
            av_tx_uninit(&m_CurVideoState->vis.rdft);
            av_freep(&m_CurVideoState->vis.real_data);
            av_freep(&m_CurVideoState->vis.rdft_data);
            m_CurVideoState->vis.rdft_bits = rdft_bits;
            m_CurVideoState->vis.real_data = reinterpret_cast<float *>(av_malloc_array(nb_freq, 4 * sizeof(*m_CurVideoState->vis.real_data)));
            m_CurVideoState->vis.rdft_data = reinterpret_cast<AVComplexFloat *>(av_malloc_array(nb_freq + 1, 2 * sizeof(*m_CurVideoState->vis.rdft_data)));
            err = av_tx_init(&m_CurVideoState->vis.rdft, &m_CurVideoState->vis.rdft_fn, AV_TX_FLOAT_RDFT,
                             0, 1 << rdft_bits, &rdft_scale, 0);
        }
        if (err < 0 || !m_CurVideoState->vis.rdft_data)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate buffers for RDFT, switching to waves display\n");
            m_CurVideoState->show_mode = VideoState::ShowMode::SHOW_MODE_WAVES;
        }
        else
        {
            float *data_in[2];
            AVComplexFloat *data[2];
            SDL_Rect rect = {.x = m_CurVideoState->vis.xpos, .y = 0, .w = 1, .h = m_CurVideoState->height};
            uint32_t *pixels;
            int pitch;
            for (ch = 0; ch < nb_display_channels; ch++)
            {
                data_in[ch] = m_CurVideoState->vis.real_data + 2 * nb_freq * ch;
                data[ch] = m_CurVideoState->vis.rdft_data + nb_freq * ch;
                i = i_start + ch;
                for (x = 0; x < 2 * nb_freq; x++)
                {
                    double w = (x - nb_freq) * (1.0 / nb_freq);
                    data_in[ch][x] = m_CurVideoState->vis.sample_array[i] * (1.0 - w * w);
                    i += channels;
                    if (i >= SAMPLE_ARRAY_SIZE)
                        i -= SAMPLE_ARRAY_SIZE;
                }
                m_CurVideoState->vis.rdft_fn(m_CurVideoState->vis.rdft, data[ch], data_in[ch], sizeof(float));
                data[ch][0].im = data[ch][nb_freq].re;
                data[ch][nb_freq].re = 0;
            }
            /* Least efficient way to do this, we should of course
             * directly access it but it is more than fast enough. */
            if (!SDL_LockTexture(m_CurVideoState->vis.vis_texture, &rect, (void **)&pixels, &pitch))
            {
                pitch >>= 2;
                pixels += pitch * m_CurVideoState->height;
                for (y = 0; y < m_CurVideoState->height; y++)
                {
                    double w = 1 / sqrt(nb_freq);
                    int a = sqrt(w * sqrt(data[0][y].re * data[0][y].re + data[0][y].im * data[0][y].im));
                    int b = (nb_display_channels == 2) ? sqrt(w * hypot(data[1][y].re, data[1][y].im))
                                                       : a;
                    a = FFMIN(a, 255);
                    b = FFMIN(b, 255);
                    pixels -= pitch;
                    *pixels = (a << 16) + (b << 8) + ((a + b) >> 1);
                }
                SDL_UnlockTexture(m_CurVideoState->vis.vis_texture);
            }
            SDL_RenderCopy(renderer, m_CurVideoState->vis.vis_texture, NULL, NULL);
        }
        if (!m_CurVideoState->paused)
            m_CurVideoState->vis.xpos++;
    }
}

void VideoCtl::video_image_display()
{
    Frame *vp;
    Frame *sp = NULL;
    SDL_Rect rect;

    vp = frame_queue_peek_last(&m_CurVideoState->video.pictq);
    if (vk_renderer)
    {
        vk_renderer_display(vk_renderer, vp->frame);
        return;
    }

    if (m_CurVideoState->subtitle.subtitle_st)
    {
        if (frame_queue_nb_remaining(&m_CurVideoState->subtitle.subpq) > 0)
        {
            sp = frame_queue_peek(&m_CurVideoState->subtitle.subpq);

            if (vp->pts >= sp->pts + ((float)sp->sub.start_display_time / 1000))
            {
                if (!sp->uploaded)
                {
                    uint8_t *pixels[4];
                    int pitch[4];
                    int i;
                    if (!sp->width || !sp->height)
                    {
                        sp->width = vp->width;
                        sp->height = vp->height;
                    }
                    if (realloc_texture(&m_CurVideoState->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
                        return;

                    for (i = 0; i < sp->sub.num_rects; i++)
                    {
                        AVSubtitleRect *sub_rect = sp->sub.rects[i];

                        sub_rect->x = av_clip(sub_rect->x, 0, sp->width);
                        sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
                        sub_rect->w = av_clip(sub_rect->w, 0, sp->width - sub_rect->x);
                        sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

                        m_CurVideoState->video.sub_convert_ctx = sws_getCachedContext(m_CurVideoState->video.sub_convert_ctx,
                                                                                      sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
                                                                                      sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
                                                                                      0, NULL, NULL, NULL);
                        if (!m_CurVideoState->video.sub_convert_ctx)
                        {
                            av_log(NULL, AV_LOG_FATAL, "Cannot initialize the conversion context\n");
                            return;
                        }
                        if (!SDL_LockTexture(m_CurVideoState->sub_texture, (SDL_Rect *)sub_rect, (void **)pixels, pitch))
                        {
                            sws_scale(m_CurVideoState->video.sub_convert_ctx, (const uint8_t *const *)sub_rect->data, sub_rect->linesize,
                                      0, sub_rect->h, pixels, pitch);
                            SDL_UnlockTexture(m_CurVideoState->sub_texture);
                        }
                    }
                    sp->uploaded = 1;
                }
            }
            else
                sp = NULL;
        }
    }

    calculate_display_rect(&rect, m_CurVideoState->xleft, m_CurVideoState->ytop, m_CurVideoState->width, m_CurVideoState->height, vp->width, vp->height, vp->sar);
    set_sdl_yuv_conversion_mode(vp->frame);

    if (!vp->uploaded)
    {
        if (upload_texture(&m_CurVideoState->video.vid_texture, vp->frame) < 0)
        {
            set_sdl_yuv_conversion_mode(NULL);
            return;
        }
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
    }

    SDL_RenderCopyEx(renderer, m_CurVideoState->video.vid_texture, NULL, &rect, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : static_cast<SDL_RendererFlip>(0));
    set_sdl_yuv_conversion_mode(NULL);
    if (sp)
    {
#if USE_ONEPASS_SUBTITLE_RENDER
        SDL_RenderCopy(renderer, m_CurVideoState->sub_texture, NULL, &rect);
#else
        int i;
        double xratio = (double)rect.w / (double)sp->width;
        double yratio = (double)rect.h / (double)sp->height;
        for (i = 0; i < sp->sub.num_rects; i++)
        {
            SDL_Rect *sub_rect = (SDL_Rect *)sp->sub.rects[i];
            SDL_Rect target = {.x = rect.x + sub_rect->x * xratio,
                               .y = rect.y + sub_rect->y * yratio,
                               .w = sub_rect->w * xratio,
                               .h = sub_rect->h * yratio};
            SDL_RenderCopy(renderer, m_CurVideoState->sub_texture, sub_rect, &target);
        }
#endif
    }
}

void VideoCtl::update_sample_display(VideoState *is, short *samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0)
    {
        len = SAMPLE_ARRAY_SIZE - is->vis.sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->vis.sample_array + is->vis.sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->vis.sample_array_index += len;
        if (is->vis.sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->vis.sample_array_index = 0;
        size -= len;
    }
}

int VideoCtl::audio_decode_frame(VideoState *is)
{
    int data_size, resampled_data_size;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;

    if (is->paused)
        return -1;

    do
    {
#if defined(_WIN32)
        while (frame_queue_nb_remaining(&is->audio.sampq) == 0)
        {
            if ((av_gettime_relative() - audio_callback_time) > 1000000LL * is->audio.audio_hw_buf_size / is->audio.audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep(1000);
        }
#endif
        if (!(af = frame_queue_peek_readable(&is->audio.sampq)))
            return -1;
        frame_queue_next(&is->audio.sampq);
    } while (af->serial != is->audio.audioq.serial);

    data_size = av_samples_get_buffer_size(NULL, af->frame->ch_layout.nb_channels,
                                           af->frame->nb_samples,
                                           static_cast<AVSampleFormat>(af->frame->format),
                                           1);

    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);

    if (af->frame->format != is->audio.audio_src.fmt ||
        av_channel_layout_compare(&af->frame->ch_layout, &is->audio.audio_src.ch_layout) ||
        af->frame->sample_rate != is->audio.audio_src.freq ||
        (wanted_nb_samples != af->frame->nb_samples && !is->audio.swr_ctx))
    {
        swr_free(&is->audio.swr_ctx);
        swr_alloc_set_opts2(&is->audio.swr_ctx,
                            &is->audio.audio_tgt.ch_layout, is->audio.audio_tgt.fmt, is->audio.audio_tgt.freq,
                            &af->frame->ch_layout, static_cast<AVSampleFormat>(af->frame->format), af->frame->sample_rate,
                            0, NULL);
        if (!is->audio.swr_ctx || swr_init(is->audio.swr_ctx) < 0)
        {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                   af->frame->sample_rate, av_get_sample_fmt_name(static_cast<AVSampleFormat>(af->frame->format)), af->frame->ch_layout.nb_channels,
                   is->audio.audio_tgt.freq, av_get_sample_fmt_name(is->audio.audio_tgt.fmt), is->audio.audio_tgt.ch_layout.nb_channels);
            swr_free(&is->audio.swr_ctx);
            return -1;
        }
        if (av_channel_layout_copy(&is->audio.audio_src.ch_layout, &af->frame->ch_layout) < 0)
            return -1;
        is->audio.audio_src.freq = af->frame->sample_rate;
        is->audio.audio_src.fmt = static_cast<AVSampleFormat>(af->frame->format);
    }

    if (is->audio.swr_ctx)
    {
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        uint8_t **out = &is->audio.audio_buf1;
        int out_count = (int64_t)wanted_nb_samples * is->audio.audio_tgt.freq / af->frame->sample_rate + 256;
        int out_size = av_samples_get_buffer_size(NULL, is->audio.audio_tgt.ch_layout.nb_channels, out_count, is->audio.audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wanted_nb_samples != af->frame->nb_samples)
        {
            if (swr_set_compensation(is->audio.swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio.audio_tgt.freq / af->frame->sample_rate,
                                     wanted_nb_samples * is->audio.audio_tgt.freq / af->frame->sample_rate) < 0)
            {
                av_log(NULL, AV_LOG_ERROR, "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&is->audio.audio_buf1, &is->audio.audio_buf1_size, out_size);
        if (!is->audio.audio_buf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(is->audio.swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count)
        {
            av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->audio.swr_ctx) < 0)
                swr_free(&is->audio.swr_ctx);
        }
        is->audio.audio_buf = is->audio.audio_buf1;
        resampled_data_size = len2 * is->audio.audio_tgt.ch_layout.nb_channels * av_get_bytes_per_sample(is->audio.audio_tgt.fmt);
    }
    else
    {
        is->audio.audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }

    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
        is->audio_clock = af->pts + (double)af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock = NAN;
    is->audio_clock_serial = af->serial;
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               is->audio_clock - last_clock,
               is->audio_clock, audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}

int VideoCtl::synchronize_audio(VideoState *is, int nb_samples)
{
    int wanted_nb_samples = nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER)
    {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = get_clock(&is->audclk) - get_master_clock(is);

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD)
        {
            is->audio.audio_diff_cum = diff + is->audio.audio_diff_avg_coef * is->audio.audio_diff_cum;
            if (is->audio.audio_diff_avg_count < AUDIO_DIFF_AVG_NB)
            {
                /* not enough measures to have a correct estimate */
                is->audio.audio_diff_avg_count++;
            }
            else
            {
                /* estimate the A-V difference */
                avg_diff = is->audio.audio_diff_cum * (1.0 - is->audio.audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio.audio_diff_threshold)
                {
                    wanted_nb_samples = nb_samples + (int)(diff * is->audio.audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }
                av_log(NULL, AV_LOG_TRACE, "diff=%f adiff=%f sample_diff=%d apts=%0.3f %f\n",
                       diff, avg_diff, wanted_nb_samples - nb_samples,
                       is->audio_clock, is->audio.audio_diff_threshold);
            }
        }
        else
        {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            is->audio.audio_diff_avg_count = 0;
            is->audio.audio_diff_cum = 0;
        }
    }

    return wanted_nb_samples;
}

double VideoCtl::get_master_clock(VideoState *is)
{
    double val;

    switch (get_master_sync_type(is))
    {
    case AV_SYNC_VIDEO_MASTER:
        val = get_clock(&is->vidclk);
        break;
    case AV_SYNC_AUDIO_MASTER:
        val = get_clock(&is->audclk);
        break;
    default:
        val = get_clock(&is->extclk);
        break;
    }
    return val;
}

int VideoCtl::get_master_sync_type(VideoState *is)
{
    if (is->av_sync_type == AV_SYNC_VIDEO_MASTER)
    {
        if (is->video.video_st)
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_AUDIO_MASTER;
    }
    else if (is->av_sync_type == AV_SYNC_AUDIO_MASTER)
    {
        if (is->audio.audio_st)
            return AV_SYNC_AUDIO_MASTER;
        else
            return AV_SYNC_EXTERNAL_CLOCK;
    }
    else
    {
        return AV_SYNC_EXTERNAL_CLOCK;
    }
}

int VideoCtl::read_thread(void *arg)
{
    VideoState *is = reinterpret_cast<VideoState *>(arg);

    // FFmpeg格式上下文，用于管理媒体文件格式
    AVFormatContext *ic = NULL;
    int err, i, ret;
    int st_index[AVMEDIA_TYPE_NB];             // 各媒体类型的最佳流索引
    AVPacket *pkt = NULL;                      // 存储从流中读取的原始数据包
    int64_t stream_start_time;                 // 流的起始时间
    int pkt_in_play_range = 0;                 // 标识数据包是否在播放时间范围内
    SDL_mutex *wait_mutex = SDL_CreateMutex(); // 同步互斥锁
    int scan_all_pmts_set = 0;
    int64_t pkt_ts;

    if (!wait_mutex)
    {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* 初始化关键数据结构 */
    memset(st_index, -1, sizeof(st_index)); // 流索引初始化为-1
    is->eof = 0;                            // 重置EOF标志

    // 创建AVPacket实例
    if (!(pkt = av_packet_alloc()))
    {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate packet.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* 初始化格式上下文 */
    if (!(ic = avformat_alloc_context()))
    {
        av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ic->interrupt_callback.callback = decode_interrupt_cb; // 设置中断回调
    ic->interrupt_callback.opaque = is;
    if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE))
    {
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }

    /* 打开媒体文件并解析格式 */
    if ((err = avformat_open_input(&ic, is->filename, is->iformat, nullptr)) < 0)
    {
        av_strerror(err, error, 128);
        av_log(nullptr, AV_LOG_DEBUG, "avformat_open_input to faild, error info: %s\n", error);
        ret = -1;
        goto fail;
    }
    is->ic = ic; // 将格式上下文绑定到视频状态对象

    if (genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

    /* 获取流信息 */
    if ((err = avformat_find_stream_info(ic, nullptr)) < 0)
    {
        av_log(NULL, AV_LOG_WARNING, "%s: could not find codec parameters\n", is->filename);
        ret = -1;
        goto fail;
    }

    // 强制重置输入层的EOF（文件结束）标记，确保后续读取操作不会因之前的探测行为导致的假性EOF状态而提前终止。
    if (ic->pb)
        ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    if (seek_by_bytes < 0)
        seek_by_bytes = !(ic->iformat->flags & AVFMT_NO_BYTE_SEEK) &&
                        !!(ic->iformat->flags & AVFMT_TS_DISCONT) &&
                        strcmp("ogg", ic->iformat->name);

    // 根据输入媒体格式的时间戳连续性特征，动态设置最大允许的帧间隔时间，用于优化播放器的同步策略。
    is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    // 设置窗口标题, 使用无边框窗口则不需要
    if (!window_title)
        window_title = input_filename;

    /* if seeking requested, we execute it */
    if (start_time != AV_NOPTS_VALUE)
    {
        int64_t timestamp;

        timestamp = start_time;
        /* add the stream start time */
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                   is->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    is->realtime = is_realtime(ic); // 判断输入媒体源是否为实时流媒体

    if (show_status)
        av_dump_format(ic, 0, is->filename, 0);

    for (i = 0; i < ic->nb_streams; i++)
    {
        AVStream *st = ic->streams[i];
        enum AVMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
        if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic, st, wanted_stream_spec[type]) > 0)
                st_index[type] = i;
    }
    for (i = 0; i < AVMEDIA_TYPE_NB; i++)
    {
        if (wanted_stream_spec[(AVMediaType)i] && st_index[(AVMediaType)i] == -1)
        {
            av_log(NULL, AV_LOG_ERROR, "Stream specifier %s does not match any %s stream\n", wanted_stream_spec[(AVMediaType)i], av_get_media_type_string((AVMediaType)i));
            st_index[(AVMediaType)i] = INT_MAX;
        }
    }

    if (!video_disable)
        st_index[AVMEDIA_TYPE_VIDEO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
    if (!audio_disable)
        st_index[AVMEDIA_TYPE_AUDIO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                NULL, 0);
    if (!video_disable && !subtitle_disable)
        st_index[AVMEDIA_TYPE_SUBTITLE] =
            av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                                st_index[AVMEDIA_TYPE_SUBTITLE],
                                (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ? st_index[AVMEDIA_TYPE_AUDIO] : st_index[AVMEDIA_TYPE_VIDEO]),
                                NULL, 0);

    is->show_mode = show_mode;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0)
    {
        AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
        AVCodecParameters *codecpar = st->codecpar;
        AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
        if (codecpar->width)
            set_default_window_size(codecpar->width, codecpar->height, sar);
    }

    /* 打开媒体流组件 */
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0)
        stream_component_open(is, st_index[AVMEDIA_TYPE_AUDIO]); // 打开音频流

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0)
        ret = stream_component_open(is, st_index[AVMEDIA_TYPE_VIDEO]); // 打开视频流

    if (is->show_mode == VideoState::ShowMode::SHOW_MODE_NONE)
        is->show_mode = ret >= 0 ? VideoState::ShowMode::SHOW_MODE_VIDEO : VideoState::ShowMode::SHOW_MODE_RDFT;

    if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0)
        stream_component_open(is, st_index[AVMEDIA_TYPE_SUBTITLE]); // 打开字幕流

    if (is->video_stream < 0 && is->audio_stream < 0)
    {
        av_log(NULL, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               is->filename);
        ret = -1;
        goto fail;
    }

    if (infinite_buffer < 0 && is->realtime)
        infinite_buffer = 1; // 开启无限缓冲区

    /* 主读取循环 */
    for (;;)
    {
        if (is->abort_request)
            break; // 收到终止请求

        /* 处理暂停状态切换 */
        if (is->paused != is->last_paused)
        {
            is->last_paused = is->paused;
            if (is->paused)
                is->read_pause_return = av_read_pause(ic);
            else
                av_read_play(ic);
        }

#if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
        if (is->paused &&
            (!strcmp(ic->iformat->name, "rtsp") ||
             (ic->pb && !strncmp(input_filename, "mmsh:", 5))))
        {
            /* wait 10 ms to avoid trying to get another packet */
            /* XXX: horrible */
            SDL_Delay(10);
            continue;
        }
#endif
        /* 处理SEEK请求 */
        if (is->seek_req)
        {
            int64_t seek_target = is->seek_pos;
            int64_t seek_min = is->seek_rel > 0 ? seek_target - is->seek_rel + 2 : INT64_MIN;
            int64_t seek_max = is->seek_rel < 0 ? seek_target - is->seek_rel - 2 : INT64_MAX;
            // FIXME the +-2 is due to rounding being not done in the correct direction in generation
            //      of the seek_pos/seek_rel variables

            ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
            if (ret < 0)
            {
                av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n", is->ic->url);
            }
            else
            {
                if (is->audio_stream >= 0)
                    packet_queue_flush(&is->audio.audioq);
                if (is->subtitle_stream >= 0)
                    packet_queue_flush(&is->subtitle.subtitleq);
                if (is->video_stream >= 0)
                    packet_queue_flush(&is->video.videoq);
                if (is->seek_flags & AVSEEK_FLAG_BYTE)
                {
                    set_clock(&is->extclk, NAN, 0);
                }
                else
                {
                    set_clock(&is->extclk, seek_target / (double)AV_TIME_BASE, 0);
                }
            }
            is->seek_req = 0;
            is->queue_attachments_req = 1;
            is->eof = 0;
            if (is->paused)
                step_to_next_frame(is);
        }

        // 处理媒体文件内嵌的附件资源（如专辑封面、静态缩略图）
        if (is->queue_attachments_req)
        {
            if (is->video.video_st && is->video.video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)
            {
                if ((ret = av_packet_ref(pkt, &is->video.video_st->attached_pic)) < 0)
                    goto fail;
                packet_queue_put(&is->video.videoq, pkt);
                packet_queue_put_nullpacket(&is->video.videoq, pkt, is->video_stream); // 发送一个空包，用于通知解码器附件数据已经处理完毕。
            }
            is->queue_attachments_req = 0;
        }

        /* if the queue are full, no need to read more */
        if (infinite_buffer < 1 &&
            (is->audio.audioq.size + is->video.videoq.size + is->subtitle.subtitleq.size > MAX_QUEUE_SIZE || (stream_has_enough_packets(is->audio.audio_st, is->audio_stream, &is->audio.audioq) &&
                                                                                                              stream_has_enough_packets(is->video.video_st, is->video_stream, &is->video.videoq) &&
                                                                                                              stream_has_enough_packets(is->subtitle.subtitle_st, is->subtitle_stream, &is->subtitle.subtitleq))))
        {
            /* wait 10 ms */
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        }
        if (!is->paused &&
            (!is->audio.audio_st || (is->audio.auddec.finished == is->audio.audioq.serial && frame_queue_nb_remaining(&is->audio.sampq) == 0)) &&
            (!is->video.video_st || (is->video.viddec.finished == is->video.videoq.serial && frame_queue_nb_remaining(&is->video.pictq) == 0)))
        {
            if (loop != 1 && (!loop || --loop))
            {
                stream_seek(is, start_time != AV_NOPTS_VALUE ? start_time : 0, 0, 0);
            }
            else if (autoexit)
            {
                ret = AVERROR_EOF;
                goto fail;
            }
        }
        /* 读取媒体帧 */
        if ((ret = av_read_frame(ic, pkt)) < 0)
        {
            // 处理流结束情况
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof)
            {
                if (is->video_stream >= 0)
                    packet_queue_put_nullpacket(&is->video.videoq, pkt, is->video_stream);
                if (is->audio_stream >= 0)
                    packet_queue_put_nullpacket(&is->audio.audioq, pkt, is->audio_stream);
                if (is->subtitle_stream >= 0)
                    packet_queue_put_nullpacket(&is->subtitle.subtitleq, pkt, is->subtitle_stream);
                is->eof = 1;
            }
            if (ic->pb && ic->pb->error)
            {
                if (autoexit)
                    goto fail;
                else
                    break;
            }
            SDL_LockMutex(wait_mutex);
            SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
            SDL_UnlockMutex(wait_mutex);
            continue;
        }
        else
        {
            is->eof = 0;
        }

        /* check if packet is in play range specified by user, then queue, otherwise discard */
        stream_start_time = ic->streams[pkt->stream_index]->start_time;
        pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        pkt_in_play_range = AV_NOPTS_VALUE == AV_NOPTS_VALUE ||
                            (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                                        av_q2d(ic->streams[pkt->stream_index]->time_base) -
                                    (double)(start_time != AV_NOPTS_VALUE ? start_time : 0) / 1000000 <=
                                ((double)duration / 1000000);
        if (pkt->stream_index == is->audio_stream && pkt_in_play_range)
        {
            packet_queue_put(&is->audio.audioq, pkt);
        }
        else if (pkt->stream_index == is->video_stream && pkt_in_play_range && !(is->video.video_st->disposition & AV_DISPOSITION_ATTACHED_PIC))
        {
            packet_queue_put(&is->video.videoq, pkt);
        }
        else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range)
        {
            packet_queue_put(&is->subtitle.subtitleq, pkt);
        }
        else
        {
            av_packet_unref(pkt);
        }
    }

    ret = 0;
fail: /* 错误处理段 */
    if (ic && !is->ic)
        avformat_close_input(&ic);

    av_packet_free(&pkt);
    if (ret != 0)
    {
        SDL_Event event;

        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    SDL_DestroyMutex(wait_mutex);
    return 0;
}

int VideoCtl::decode_interrupt_cb(void *ctx)
{
    VideoState *is = reinterpret_cast<VideoState*>(ctx);
    return is->abort_request;
}

void VideoCtl::stream_seek(VideoState *is, int64_t pos, int64_t rel, int by_bytes)
{
    if (!is->seek_req)
    {
        is->seek_pos = pos;
        is->seek_rel = rel;
        is->seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (by_bytes)
            is->seek_flags |= AVSEEK_FLAG_BYTE;
        is->seek_req = 1;
        SDL_CondSignal(is->continue_read_thread);
    }
}

void VideoCtl::step_to_next_frame(VideoState *is)
{
    /* if the stream is paused unpause it, then step */
    if (is->paused)
        stream_toggle_pause(is);
    is->step = 1;
}

void VideoCtl::stream_toggle_pause(VideoState *is)
{
    if (is->paused) {
        is->video.frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
        if (is->read_pause_return != AVERROR(ENOSYS)) {
            is->vidclk.paused = 0;
        }
        set_clock(&is->vidclk, get_clock(&is->vidclk), is->vidclk.serial);
    }
    set_clock(&is->extclk, get_clock(&is->extclk), is->extclk.serial);
    is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

int VideoCtl::stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue)
{
    return stream_id < 0 ||
           queue->abort_request ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
           queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
}

int VideoCtl::stream_component_open(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic;
    AVCodecContext *avctx;
    const AVCodec *codec;
    const char *forced_codec_name = NULL;
    AVDictionary *opts = NULL;
    const AVDictionaryEntry *t = NULL;
    int sample_rate;
    AVChannelLayout ch_layout;
    memset(&ch_layout, 0, sizeof(AVChannelLayout));
    int ret = 0;
    int stream_lowres = 0;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;
    avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    codec = avcodec_find_decoder(avctx->codec_id);

    switch(avctx->codec_type){
        case AVMEDIA_TYPE_AUDIO   : is->last_audio_stream    = stream_index; break;
        case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; break;
        case AVMEDIA_TYPE_VIDEO   : is->last_video_stream    = stream_index; break;
    }
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        if (forced_codec_name) av_log(NULL, AV_LOG_WARNING,
                   "No codec could be found with name '%s'\n", forced_codec_name);
        else                   av_log(NULL, AV_LOG_WARNING,
                   "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    avctx->codec_id = codec->id;
    if (stream_lowres > codec->max_lowres) {
        av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
               codec->max_lowres);
        stream_lowres = codec->max_lowres;
    }
    avctx->lowres = stream_lowres;

    if (fast)
        avctx->flags2 |= AV_CODEC_FLAG2_FAST;

    ret = filter_codec_opts(codec_opts, avctx->codec_id, ic,
                            ic->streams[stream_index], codec, &opts);
    if (ret < 0)
        goto fail;

    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);

    av_dict_set(&opts, "flags", "+copy_opaque", AV_DICT_MULTIKEY);

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = create_hwaccel(&avctx->hw_device_ctx);
        if (ret < 0)
            goto fail;
    }

    
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        ret =  AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    is->eof = 0;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        {
            AVFilterContext *sink;

            is->audio.audio_filter_src.freq           = avctx->sample_rate;
            ret = av_channel_layout_copy(&is->audio.audio_filter_src.ch_layout, &avctx->ch_layout);
            if (ret < 0)
                goto fail;
            is->audio.audio_filter_src.fmt            = avctx->sample_fmt;
            if ((ret = configure_audio_filters(is, afilters, 0)) < 0)
                goto fail;
            sink = is->out_audio_filter;
            sample_rate    = av_buffersink_get_sample_rate(sink);
            ret = av_buffersink_get_ch_layout(sink, &ch_layout);
            if (ret < 0)
                goto fail;
        }

        /* prepare audio output */
        if ((ret = audio_open(is, &ch_layout, sample_rate, &is->audio.audio_tgt)) < 0)
            goto fail;
        is->audio.audio_hw_buf_size = ret;
        is->audio.audio_src = is->audio.audio_tgt;
        is->audio.audio_buf_size  = 0;
        is->audio.audio_buf_index = 0;

        /* init averaging filter */
        is->audio.audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio.audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio FIFO fullness,
           we correct audio sync only if larger than this threshold */
        is->audio.audio_diff_threshold = (double)(is->audio.audio_hw_buf_size) / is->audio.audio_tgt.bytes_per_sec;

        is->audio_stream = stream_index;
        is->audio.audio_st = ic->streams[stream_index];

        if ((ret = decoder_init(&is->audio.auddec, avctx, &is->audio.audioq, is->continue_read_thread)) < 0)
            goto fail;
        if (is->ic->iformat->flags & AVFMT_NOTIMESTAMPS) {
            is->audio.auddec.start_pts = is->audio.audio_st->start_time;
            is->audio.auddec.start_pts_tb = is->audio.audio_st->time_base;
        }
        if ((ret = decoder_start(&is->audio.auddec, audio_thread, "audio_decoder", is)) < 0)
            goto out;
        SDL_PauseAudioDevice(audio_dev, 0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video.video_st = ic->streams[stream_index];

        if ((ret = decoder_init(&is->video.viddec, avctx, &is->video.videoq, is->continue_read_thread)) < 0)
            goto fail;
        if ((ret = decoder_start(&is->video.viddec, video_thread, "video_decoder", is)) < 0)
            goto out;
        is->queue_attachments_req = 1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_stream = stream_index;
        is->subtitle.subtitle_st = ic->streams[stream_index];

        if ((ret = decoder_init(&is->subtitle.subdec, avctx, &is->subtitle.subtitleq, is->continue_read_thread)) < 0)
            goto fail;
        if ((ret = decoder_start(&is->subtitle.subdec, subtitle_thread, "subtitle_decoder", is)) < 0)
            goto out;
        break;
    default:
        break;
        }
    goto out;

fail:
    avcodec_free_context(&avctx);
out:
    av_channel_layout_uninit(&ch_layout);
    av_dict_free(&opts);

    return ret;
}

int VideoCtl::audio_thread(void *arg)
{
    VideoState *is = reinterpret_cast<VideoState *>(arg);
    AVFrame *frame = av_frame_alloc();
    Frame *af;
    int last_serial = -1;
    int reconfigure;
    int got_frame = 0;
    AVRational tb;
    int ret = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    do
    {
        if ((got_frame = decoder_decode_frame(&is->audio.auddec, frame, NULL)) < 0)
            goto the_end;

        if (got_frame)
        {
            tb = (AVRational){1, frame->sample_rate};

            reconfigure =
                cmp_audio_fmts(is->audio.audio_filter_src.fmt, is->audio.audio_filter_src.ch_layout.nb_channels,
                               static_cast<AVSampleFormat>(frame->format), frame->ch_layout.nb_channels) ||
                av_channel_layout_compare(&is->audio.audio_filter_src.ch_layout, &frame->ch_layout) ||
                is->audio.audio_filter_src.freq != frame->sample_rate ||
                is->audio.auddec.pkt_serial != last_serial;

            if (reconfigure)
            {
                char buf1[1024], buf2[1024];
                av_channel_layout_describe(&is->audio.audio_filter_src.ch_layout, buf1, sizeof(buf1));
                av_channel_layout_describe(&frame->ch_layout, buf2, sizeof(buf2));
                av_log(NULL, AV_LOG_DEBUG,
                       "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                       is->audio.audio_filter_src.freq, is->audio.audio_filter_src.ch_layout.nb_channels, av_get_sample_fmt_name(is->audio.audio_filter_src.fmt), buf1, last_serial,
                       frame->sample_rate, frame->ch_layout.nb_channels, av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format)), buf2, is->audio.auddec.pkt_serial);

                is->audio.audio_filter_src.fmt = static_cast<AVSampleFormat>(frame->format);
                ret = av_channel_layout_copy(&is->audio.audio_filter_src.ch_layout, &frame->ch_layout);
                if (ret < 0)
                    goto the_end;
                is->audio.audio_filter_src.freq = frame->sample_rate;
                last_serial = is->audio.auddec.pkt_serial;

                if ((ret = configure_audio_filters(is, afilters, 1)) < 0)
                    goto the_end;
            }

            if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
                goto the_end;

            while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0)
            {
                FrameData *fd = frame->opaque_ref ? (FrameData *)frame->opaque_ref->data : NULL;
                tb = av_buffersink_get_time_base(is->out_audio_filter);
                if (!(af = frame_queue_peek_writable(&is->audio.sampq)))
                    goto the_end;

                af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                af->pos = fd ? fd->pkt_pos : -1;
                af->serial = is->audio.auddec.pkt_serial;
                af->duration = av_q2d((AVRational){frame->nb_samples, frame->sample_rate});

                av_frame_move_ref(af->frame, frame);
                frame_queue_push(&is->audio.sampq);

                if (is->audio.audioq.serial != is->audio.auddec.pkt_serial)
                    break;
            }
            if (ret == AVERROR_EOF)
                is->audio.auddec.finished = is->audio.auddec.pkt_serial;
        }
    } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
the_end:
    avfilter_graph_free(&is->agraph);
    av_frame_free(&frame);
    return ret;
}

inline int VideoCtl::cmp_audio_fmts(AVSampleFormat fmt1, int64_t channel_count1, AVSampleFormat fmt2, int64_t channel_count2)
{
    /* If channel count == 1, planar and non-planar formats are the same */
    if (channel_count1 == 1 && channel_count2 == 1)
        return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
    else
        return channel_count1 != channel_count2 || fmt1 != fmt2;
}

int VideoCtl::video_thread(void *arg)
{
    VideoState *is = reinterpret_cast<VideoState *>(arg);
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret;
    AVRational tb = is->video.video_st->time_base;
    AVRational frame_rate = av_guess_frame_rate(is->ic, is->video.video_st, NULL);

    AVFilterGraph *graph = NULL;
    AVFilterContext *filt_out = NULL, *filt_in = NULL;
    int last_w = 0;
    int last_h = 0;
    enum AVPixelFormat last_format = static_cast<AVPixelFormat>(-2);
    int last_serial = -1;
    int last_vfilter_idx = 0;

    if (!frame)
        return AVERROR(ENOMEM);

    for (;;)
    {
        ret = get_video_frame(is, frame);
        if (ret < 0)
            goto the_end;
        if (!ret)
            continue;

        if (last_w != frame->width || last_h != frame->height || last_format != frame->format || last_serial != is->video.viddec.pkt_serial || last_vfilter_idx != is->vfilter_idx)
        {
            av_log(NULL, AV_LOG_DEBUG,
                   "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                   last_w, last_h,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                   frame->width, frame->height,
                   (const char *)av_x_if_null(av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format)), "none"), is->video.viddec.pkt_serial);
            avfilter_graph_free(&graph);
            graph = avfilter_graph_alloc();
            if (!graph)
            {
                ret = AVERROR(ENOMEM);
                goto the_end;
            }
            graph->nb_threads = filter_nbthreads;
            if ((ret = configure_video_filters(graph, is, vfilters_list ? vfilters_list[is->vfilter_idx] : NULL, frame)) < 0)
            {
                SDL_Event event;
                event.type = FF_QUIT_EVENT;
                event.user.data1 = is;
                SDL_PushEvent(&event);
                goto the_end;
            }
            filt_in = is->in_video_filter;
            filt_out = is->out_video_filter;
            last_w = frame->width;
            last_h = frame->height;
            last_format = static_cast<AVPixelFormat>(frame->format);
            last_serial = is->video.viddec.pkt_serial;
            last_vfilter_idx = is->vfilter_idx;
            frame_rate = av_buffersink_get_frame_rate(filt_out);
        }

        ret = av_buffersrc_add_frame(filt_in, frame);
        if (ret < 0)
            goto the_end;

        while (ret >= 0)
        {
            FrameData *fd;

            is->video.frame_last_returned_time = av_gettime_relative() / 1000000.0;

            ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
            if (ret < 0)
            {
                if (ret == AVERROR_EOF)
                    is->video.viddec.finished = is->video.viddec.pkt_serial;
                ret = 0;
                break;
            }

            fd = frame->opaque_ref ? (FrameData *)frame->opaque_ref->data : NULL;

            is->video.frame_last_filter_delay = av_gettime_relative() / 1000000.0 - is->video.frame_last_returned_time;
            if (fabs(is->video.frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
                is->video.frame_last_filter_delay = 0;
            tb = av_buffersink_get_time_base(filt_out);
            duration = (frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0);
            pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            ret = queue_picture(is, frame, pts, duration, fd ? fd->pkt_pos : -1, is->video.viddec.pkt_serial);
            av_frame_unref(frame);
            if (is->video.videoq.serial != is->video.viddec.pkt_serial)
                break;
        }

        if (ret < 0)
            goto the_end;
    }
the_end:
    avfilter_graph_free(&graph);
    av_frame_free(&frame);
    return 0;
}

int VideoCtl::get_video_frame(VideoState *is, AVFrame *frame)
{
    int got_picture;

    if ((got_picture = decoder_decode_frame(&is->video.viddec, frame, NULL)) < 0)
        return -1;

    if (got_picture)
    {
        double dpts = NAN;

        if (frame->pts != AV_NOPTS_VALUE)
            dpts = av_q2d(is->video.video_st->time_base) * frame->pts;

        frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video.video_st, frame);

        if (framedrop > 0 || (framedrop && get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER))
        {
            if (frame->pts != AV_NOPTS_VALUE)
            {
                double diff = dpts - get_master_clock(is);
                if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                    diff - is->video.frame_last_filter_delay < 0 &&
                    is->video.viddec.pkt_serial == is->vidclk.serial &&
                    is->video.videoq.nb_packets)
                {
                    is->video.frame_drops_early++;
                    av_frame_unref(frame);
                    got_picture = 0;
                }
            }
        }
    }

    return got_picture;
}

int VideoCtl::queue_picture(VideoState *is, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;

#if defined(DEBUG_SYNC)
    printf("frame_type=%c pts=%0.3f\n",
           av_get_picture_type_char(src_frame->pict_type), pts);
#endif

    if (!(vp = frame_queue_peek_writable(&is->video.pictq)))
        return -1;

    vp->sar = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;

    set_default_window_size(vp->width, vp->height, vp->sar);

    av_frame_move_ref(vp->frame, src_frame);
    frame_queue_push(&is->video.pictq);
    return 0;
}

void VideoCtl::set_default_window_size(int width, int height, AVRational sar)
{
    SDL_Rect rect;
    int max_width = screen_width ? screen_width : INT_MAX;
    int max_height = screen_height ? screen_height : INT_MAX;
    if (max_width == INT_MAX && max_height == INT_MAX)
        max_height = height;
    calculate_display_rect(&rect, 0, 0, max_width, max_height, width, height, sar);
    default_width = rect.w;
    default_height = rect.h;
}

int VideoCtl::subtitle_thread(void *arg)
{
    VideoState *is = reinterpret_cast<VideoState *>(arg);
    Frame *sp;
    int got_subtitle;
    double pts;

    for (;;)
    {
        if (!(sp = frame_queue_peek_writable(&is->subtitle.subpq)))
            return 0;

        if ((got_subtitle = decoder_decode_frame(&is->subtitle.subdec, NULL, &sp->sub)) < 0)
            break;

        pts = 0;

        if (got_subtitle && sp->sub.format == 0)
        {
            if (sp->sub.pts != AV_NOPTS_VALUE)
                pts = sp->sub.pts / (double)AV_TIME_BASE;
            sp->pts = pts;
            sp->serial = is->subtitle.subdec.pkt_serial;
            sp->width = is->subtitle.subdec.avctx->width;
            sp->height = is->subtitle.subdec.avctx->height;
            sp->uploaded = 0;

            /* now we can update the picture count */
            frame_queue_push(&is->subtitle.subpq);
        }
        else if (got_subtitle)
        {
            avsubtitle_free(&sp->sub);
        }
    }
    return 0;
}

int VideoCtl::upload_texture(SDL_Texture **tex, AVFrame *frame)
{
    int ret = 0;
    Uint32 sdl_pix_fmt;
    SDL_BlendMode sdl_blendmode;
    get_sdl_pix_fmt_and_blendmode(frame->format, &sdl_pix_fmt, &sdl_blendmode);
    if (realloc_texture(tex, sdl_pix_fmt == SDL_PIXELFORMAT_UNKNOWN ? SDL_PIXELFORMAT_ARGB8888 : sdl_pix_fmt, frame->width, frame->height, sdl_blendmode, 0) < 0)
        return -1;
    switch (sdl_pix_fmt)
    {
    case SDL_PIXELFORMAT_IYUV:
        if (frame->linesize[0] > 0 && frame->linesize[1] > 0 && frame->linesize[2] > 0)
        {
            ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0], frame->linesize[0],
                                       frame->data[1], frame->linesize[1],
                                       frame->data[2], frame->linesize[2]);
        }
        else if (frame->linesize[0] < 0 && frame->linesize[1] < 0 && frame->linesize[2] < 0)
        {
            ret = SDL_UpdateYUVTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0],
                                       frame->data[1] + frame->linesize[1] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[1],
                                       frame->data[2] + frame->linesize[2] * (AV_CEIL_RSHIFT(frame->height, 1) - 1), -frame->linesize[2]);
        }
        else
        {
            av_log(NULL, AV_LOG_ERROR, "Mixed negative and positive linesizes are not supported.\n");
            return -1;
        }
        break;
    default:
        if (frame->linesize[0] < 0)
        {
            ret = SDL_UpdateTexture(*tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
        }
        else
        {
            ret = SDL_UpdateTexture(*tex, NULL, frame->data[0], frame->linesize[0]);
        }
        break;
    }
    return ret;
}

int VideoCtl::realloc_texture(SDL_Texture **texture, Uint32 new_format,
                              int new_width, int new_height, SDL_BlendMode blendmode, int init_texture)
{
    Uint32 format;
    int access, w, h;
    if (!*texture || SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format)
    {
        void *pixels;
        int pitch;
        if (*texture)
            SDL_DestroyTexture(*texture);
        if (!(*texture = SDL_CreateTexture(renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
            return -1;
        if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
            return -1;
        if (init_texture)
        {
            if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
                return -1;
            memset(pixels, 0, pitch * new_height);
            SDL_UnlockTexture(*texture);
        }
        av_log(NULL, AV_LOG_VERBOSE, "Created %dx%d texture with %s.\n", new_width, new_height, SDL_GetPixelFormatName(new_format));
    }
    return 0;
}

void VideoCtl::get_sdl_pix_fmt_and_blendmode(int format, Uint32 *sdl_pix_fmt, SDL_BlendMode *sdl_blendmode)
{
    int i;
    *sdl_blendmode = SDL_BLENDMODE_NONE;
    *sdl_pix_fmt = SDL_PIXELFORMAT_UNKNOWN;
    if (format == AV_PIX_FMT_RGB32 ||
        format == AV_PIX_FMT_RGB32_1 ||
        format == AV_PIX_FMT_BGR32 ||
        format == AV_PIX_FMT_BGR32_1)
        *sdl_blendmode = SDL_BLENDMODE_BLEND;
    for (i = 0; i < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; i++)
    {
        if (format == sdl_texture_format_map[i].format)
        {
            *sdl_pix_fmt = sdl_texture_format_map[i].texture_fmt;
            return;
        }
    }
}

void VideoCtl::set_sdl_yuv_conversion_mode(AVFrame *frame)
{
#if SDL_VERSION_ATLEAST(2, 0, 8)
    SDL_YUV_CONVERSION_MODE mode = SDL_YUV_CONVERSION_AUTOMATIC;
    if (frame && (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUYV422 || frame->format == AV_PIX_FMT_UYVY422))
    {
        if (frame->color_range == AVCOL_RANGE_JPEG)
            mode = SDL_YUV_CONVERSION_JPEG;
        else if (frame->colorspace == AVCOL_SPC_BT709)
            mode = SDL_YUV_CONVERSION_BT709;
        else if (frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M)
            mode = SDL_YUV_CONVERSION_BT601;
    }
    SDL_SetYUVConversionMode(mode); /* FIXME: no support for linear transfer */
#endif
}

void VideoCtl::calculate_display_rect(SDL_Rect *rect,
                                      int scr_xleft, int scr_ytop,
                                      int scr_width, int scr_height,
                                      int pic_width, int pic_height,
                                      AVRational pic_sar)
{
    AVRational aspect_ratio = pic_sar; // 显示比例
    int64_t width, height, x, y;

    // 判断显示比例，如果显示比例有问题就使用1:1；
    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0)
    {
        aspect_ratio = av_make_q(1, 1);
    }
    // 等比例缩放 av_mul_q(a,b) = a*b
    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(pic_width, pic_height));

    /* XXX: we suppose the screen has a 1.0 pixel ratio */
    height = scr_height;
    width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1; // 确保宽是偶数
    if (width > scr_width)                                               // 如果宽度大于屏幕宽度就使用屏幕宽度，并重新计算高度
    {
        width = scr_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1; // 确保高是偶数
    }
    // 确保显示矩形显示在屏幕中央
    x = (scr_width - width) / 2;
    y = (scr_height - height) / 2;
    rect->x = scr_xleft + x;
    rect->y = scr_ytop + y;
    rect->w = FFMAX((int)width, 1); // 最小为1个像素点大小
    rect->h = FFMAX((int)height, 1);
}

int VideoCtl::configure_video_filters(AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame)
{
    enum AVPixelFormat pix_fmts[FF_ARRAY_ELEMS(sdl_texture_format_map)];
    char sws_flags_str[512] = "";
    char buffersrc_args[256];
    int ret;
    AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
    AVCodecParameters *codecpar = is->video.video_st->codecpar;
    AVRational fr = av_guess_frame_rate(is->ic, is->video.video_st, NULL);
    const AVDictionaryEntry *e = NULL;
    int nb_pix_fmts = 0;
    int i, j;
    AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();

    if (!par)
        return AVERROR(ENOMEM);

    for (i = 0; i < renderer_info.num_texture_formats; i++)
    {
        for (j = 0; j < FF_ARRAY_ELEMS(sdl_texture_format_map) - 1; j++)
        {
            if (renderer_info.texture_formats[i] == sdl_texture_format_map[j].texture_fmt)
            {
                pix_fmts[nb_pix_fmts++] = sdl_texture_format_map[j].format;
                break;
            }
        }
    }
    pix_fmts[nb_pix_fmts] = AV_PIX_FMT_NONE;

    while ((e = av_dict_iterate(sws_dict, e)))
    {
        if (!strcmp(e->key, "sws_flags"))
        {
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", "flags", e->value);
        }
        else
            av_strlcatf(sws_flags_str, sizeof(sws_flags_str), "%s=%s:", e->key, e->value);
    }
    if (strlen(sws_flags_str))
        sws_flags_str[strlen(sws_flags_str) - 1] = '\0';

    graph->scale_sws_opts = av_strdup(sws_flags_str);

    snprintf(buffersrc_args, sizeof(buffersrc_args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:"
             "colorspace=%d:range=%d",
             frame->width, frame->height, frame->format,
             is->video.video_st->time_base.num, is->video.video_st->time_base.den,
             codecpar->sample_aspect_ratio.num, FFMAX(codecpar->sample_aspect_ratio.den, 1),
             frame->colorspace, frame->color_range);
    if (fr.num && fr.den)
        av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", fr.num, fr.den);

    if ((ret = avfilter_graph_create_filter(&filt_src,
                                            avfilter_get_by_name("buffer"),
                                            "ffplay_buffer", buffersrc_args, NULL,
                                            graph)) < 0)
        goto fail;
    par->hw_frames_ctx = frame->hw_frames_ctx;
    ret = av_buffersrc_parameters_set(filt_src, par);
    if (ret < 0)
        goto fail;

    ret = avfilter_graph_create_filter(&filt_out,
                                       avfilter_get_by_name("buffersink"),
                                       "ffplay_buffersink", NULL, NULL, graph);
    if (ret < 0)
        goto fail;

    if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;
    if (!vk_renderer &&
        (ret = av_opt_set_int_list(filt_out, "color_spaces", sdl_supported_color_spaces, AVCOL_SPC_UNSPECIFIED, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;

    last_filter = filt_out;

/* Note: this macro adds a filter before the lastly added filter, so the
 * processing order of the filters is in reverse */
#define INSERT_FILT(name, arg)                                                \
    do                                                                        \
    {                                                                         \
        AVFilterContext *filt_ctx;                                            \
                                                                              \
        ret = avfilter_graph_create_filter(&filt_ctx,                         \
                                           avfilter_get_by_name(name),        \
                                           "ffplay_" name, arg, NULL, graph); \
        if (ret < 0)                                                          \
            goto fail;                                                        \
                                                                              \
        ret = avfilter_link(filt_ctx, 0, last_filter, 0);                     \
        if (ret < 0)                                                          \
            goto fail;                                                        \
                                                                              \
        last_filter = filt_ctx;                                               \
    } while (0)

// 自动旋转相关代码,需要移植avuitil模块下的display.c的源码
#if defined(AUTOROTATE)
    if (autorotate)
    {
        double theta = 0.0;
        int32_t *displaymatrix = NULL;
        AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);
        if (sd)
            displaymatrix = (int32_t *)sd->data;
        if (!displaymatrix)
        {
            const AVPacketSideData *psd = av_packet_side_data_get(is->video.video_st->codecpar->coded_side_data,
                                                                  is->video.video_st->codecpar->nb_coded_side_data,
                                                                  AV_PKT_DATA_DISPLAYMATRIX);
            if (psd)
                displaymatrix = (int32_t *)psd->data;
        }
        theta = get_rotation(displaymatrix);

        if (fabs(theta - 90) < 1.0)
        {
            INSERT_FILT("transpose", "clock");
        }
        else if (fabs(theta - 180) < 1.0)
        {
            INSERT_FILT("hflip", NULL);
            INSERT_FILT("vflip", NULL);
        }
        else if (fabs(theta - 270) < 1.0)
        {
            INSERT_FILT("transpose", "cclock");
        }
        else if (fabs(theta) > 1.0)
        {
            char rotate_buf[64];
            snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
            INSERT_FILT("rotate", rotate_buf);
        }
    }
#endif

    if ((ret = configure_filtergraph(graph, vfilters, filt_src, last_filter)) < 0)
        goto fail;

    is->in_video_filter = filt_src;
    is->out_video_filter = filt_out;

fail:
    av_freep(&par);
    return ret;
}

inline void VideoCtl::fill_rectangle(int x, int y, int w, int h)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    if (w && h)
        SDL_RenderFillRect(renderer, &rect);
}

inline int VideoCtl::compute_mod(int a, int b)
{
    return a < 0 ? a % b + b : a % b;
}

int VideoCtl::configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                    AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
    int ret, i;
    int nb_filters = graph->nb_filters;
    AVFilterInOut *outputs = NULL, *inputs = NULL;

    if (filtergraph)
    {
        outputs = avfilter_inout_alloc();
        inputs = avfilter_inout_alloc();
        if (!outputs || !inputs)
        {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        outputs->name = av_strdup("in");
        outputs->filter_ctx = source_ctx;
        outputs->pad_idx = 0;
        outputs->next = NULL;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink_ctx;
        inputs->pad_idx = 0;
        inputs->next = NULL;

        if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
            goto fail;
    }
    else
    {
        if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
            goto fail;
    }

    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext *, graph->filters[i], graph->filters[i + nb_filters]);

    ret = avfilter_graph_config(graph, NULL);
fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    return ret;
}

int VideoCtl::configure_audio_filters(VideoState *is, const char *afilters, int force_output_format)
{
    static const enum AVSampleFormat sample_fmts[] = {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE};
    int sample_rates[2] = {0, -1};
    AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
    char aresample_swr_opts[512] = "";
    const AVDictionaryEntry *e = NULL;
    AVBPrint bp;
    char asrc_args[256];
    int ret;

    avfilter_graph_free(&is->agraph);
    if (!(is->agraph = avfilter_graph_alloc()))
        return AVERROR(ENOMEM);
    is->agraph->nb_threads = filter_nbthreads;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);

    while ((e = av_dict_iterate(swr_opts, e)))
        av_strlcatf(aresample_swr_opts, sizeof(aresample_swr_opts), "%s=%s:", e->key, e->value);
    if (strlen(aresample_swr_opts))
        aresample_swr_opts[strlen(aresample_swr_opts) - 1] = '\0';
    av_opt_set(is->agraph, "aresample_swr_opts", aresample_swr_opts, 0);

    av_channel_layout_describe_bprint(&is->audio.audio_filter_src.ch_layout, &bp);

    ret = snprintf(asrc_args, sizeof(asrc_args),
                   "sample_rate=%d:sample_fmt=%s:time_base=%d/%d:channel_layout=%s",
                   is->audio.audio_filter_src.freq, av_get_sample_fmt_name(is->audio.audio_filter_src.fmt),
                   1, is->audio.audio_filter_src.freq, bp.str);

    ret = avfilter_graph_create_filter(&filt_asrc,
                                       avfilter_get_by_name("abuffer"), "ffplay_abuffer",
                                       asrc_args, NULL, is->agraph);
    if (ret < 0)
        goto end;

    ret = avfilter_graph_create_filter(&filt_asink,
                                       avfilter_get_by_name("abuffersink"), "ffplay_abuffersink",
                                       NULL, NULL, is->agraph);
    if (ret < 0)
        goto end;

    if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;
    if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto end;

    if (force_output_format)
    {
        av_bprint_clear(&bp);
        av_channel_layout_describe_bprint(&is->audio.audio_tgt.ch_layout, &bp);
        sample_rates[0] = is->audio.audio_tgt.freq;
        if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set(filt_asink, "ch_layouts", bp.str, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
        if ((ret = av_opt_set_int_list(filt_asink, "sample_rates", sample_rates, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
            goto end;
    }

    if ((ret = configure_filtergraph(is->agraph, afilters, filt_asrc, filt_asink)) < 0)
        goto end;

    is->in_audio_filter = filt_asrc;
    is->out_audio_filter = filt_asink;

end:
    if (ret < 0)
        avfilter_graph_free(&is->agraph);
    av_bprint_finalize(&bp, NULL);

    return ret;
}

int VideoCtl::check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
{
    int ret = avformat_match_stream_specifier(s, st, spec);
    if (ret < 0)
        av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    return ret;
}

int VideoCtl::create_hwaccel(AVBufferRef **device_ctx)
{
    enum AVHWDeviceType type;
    int ret;
    AVBufferRef *vk_dev;

    *device_ctx = NULL;

    if (!hwaccel)
        return 0;

    type = av_hwdevice_find_type_by_name(hwaccel);
    if (type == AV_HWDEVICE_TYPE_NONE)
        return AVERROR(ENOTSUP);

    ret = vk_renderer_get_hw_dev(vk_renderer, &vk_dev);
    if (ret < 0)
        return ret;

    ret = av_hwdevice_ctx_create_derived(device_ctx, type, vk_dev, 0);
    if (!ret)
        return 0;

    if (ret != AVERROR(ENOSYS))
        return ret;

    av_log(NULL, AV_LOG_WARNING, "Derive %s from vulkan not supported.\n", hwaccel);
    ret = av_hwdevice_ctx_create(device_ctx, type, NULL, NULL, 0);
    return ret;
}

int VideoCtl::filter_codec_opts(const AVDictionary *opts, AVCodecID codec_id, AVFormatContext *s, AVStream *st, const AVCodec *codec, AVDictionary **dst)
{
    AVDictionary *ret = NULL;
    const AVDictionaryEntry *t = NULL;
    int flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM
                           : AV_OPT_FLAG_DECODING_PARAM;
    char prefix = 0;
    const AVClass *cc = avcodec_get_class();

    if (!codec)
        codec = s->oformat ? avcodec_find_encoder(codec_id)
                           : avcodec_find_decoder(codec_id);

    switch (st->codecpar->codec_type)
    {
    case AVMEDIA_TYPE_VIDEO:
        prefix = 'v';
        flags |= AV_OPT_FLAG_VIDEO_PARAM;
        break;
    case AVMEDIA_TYPE_AUDIO:
        prefix = 'a';
        flags |= AV_OPT_FLAG_AUDIO_PARAM;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        prefix = 's';
        flags |= AV_OPT_FLAG_SUBTITLE_PARAM;
        break;
    }

    while (t = av_dict_iterate(opts, t))
    {
        const AVClass *priv_class;
        char *p = strchr(t->key, ':');

        /* check stream specification in opt name */
        if (p)
        {
            int err = check_stream_specifier(s, st, p + 1);
            if (err < 0)
            {
                av_dict_free(&ret);
                return err;
            }
            else if (!err)
                continue;

            *p = 0;
        }

        if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
            !codec ||
            ((priv_class = codec->priv_class) &&
             av_opt_find(&priv_class, t->key, NULL, flags,
                         AV_OPT_SEARCH_FAKE_OBJ)))
            av_dict_set(&ret, t->key, t->value, 0);
        else if (t->key[0] == prefix &&
                 av_opt_find(&cc, t->key + 1, NULL, flags,
                             AV_OPT_SEARCH_FAKE_OBJ))
            av_dict_set(&ret, t->key + 1, t->value, 0);

        if (p)
            *p = ':';
    }

    *dst = ret;
    return 0;
}

void VideoCtl::toggle_mute()
{
    m_CurVideoState->audio.muted = !m_CurVideoState->audio.muted;
}

void VideoCtl::toggle_pause()
{
    stream_toggle_pause(m_CurVideoState);
    m_CurVideoState->step = 0;
}
