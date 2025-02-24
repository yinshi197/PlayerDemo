#ifndef VIDEOCTL_H
#define VIDEOCTL_H

#include <QObject>
#include <QThread>
#include <QString>
#include "datactl.h"
#include "globalhelper.h"

class VideoCtl : public QObject
{
    Q_OBJECT

public:
    static VideoCtl *GetInstance();
    ~VideoCtl();


    static int read_thread_wrapper(void *arg);
    static int audio_thread_wrapper(void *arg);
    static int video_thread_wrapper(void *arg);
    static int subtitle_thread_wrapper(void *arg);

    void StartPlay(QString strFileName, WId widPlayWid);
    void StopPlay();

    void LoopThread(VideoState* CurStream);

    int audio_open(void *opaque, AVChannelLayout *wanted_channel_layout,
                   int wanted_sample_rate, struct AudioParams *audio_hw_params);

    void set_clock_at(Clock *c, double pts, int serial, double time);
    void sync_clock_to_slave(Clock *c, Clock *slave);
    double get_clock(Clock *c);
    void set_clock(Clock *c, double pts, int serial);

    void update_sample_display(VideoState *is, short *samples, int samples_size);

    int audio_decode_frame(VideoState *is);
    int synchronize_audio(VideoState *is, int nb_samples);
    double get_master_clock(VideoState *is);
    int get_master_sync_type(VideoState *is);

    int read_thread(void *arg);

    int decoder_start(Decoder *d, int (*fn)(void *), const char *thread_name, void *arg);

    void stream_seek(VideoState *is, int64_t pos, int64_t rel, int by_bytes);
    void step_to_next_frame(VideoState *is);
    void stream_toggle_pause(VideoState *is);
    int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);

    int stream_component_open(VideoState *is, int stream_index);

    int audio_thread(void *arg);
    inline int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                              enum AVSampleFormat fmt2, int64_t channel_count2);
    int configure_audio_filters(VideoState *is, const char *afilters, int force_output_format);

    int video_thread(void *arg);
    int get_video_frame(VideoState *is, AVFrame *frame);
    int queue_picture(VideoState *is, AVFrame *src_frame,
                      double pts, double duration, int64_t pos, int serial);
    void set_default_window_size(int width, int height, AVRational sar);
    void calculate_display_rect(SDL_Rect *rect,
                                int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                int pic_width, int pic_height, AVRational pic_sar);
    int configure_video_filters(AVFilterGraph *graph,
                                VideoState *is, const char *vfilters, AVFrame *frame);
    int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                              AVFilterContext *source_ctx, AVFilterContext *sink_ctx);

    int subtitle_thread(void *arg);

    int filter_codec_opts(const AVDictionary *opts, enum AVCodecID codec_id,
                          AVFormatContext *s, AVStream *st, const AVCodec *codec,
                          AVDictionary **dst);

    int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec);
    int create_hwaccel(AVBufferRef **device_ctx);

    void toggle_pause(VideoState *is);
    void do_exit(VideoState *is);

    void stream_cycle_channel(int codec_type);
    void toggle_audio_display();

    VideoState *m_CurVideoState = nullptr;
    bool m_playLoopIndex;

private:
    explicit VideoCtl(QObject *parent = nullptr);

    bool Init();
    bool ConnectSignalSlots();

    void UpdateVolume(int sign, double step);

    void refresh_loop_wait_event(VideoState* is, SDL_Event* event);

    VideoState* stream_open(const char *filename, const AVInputFormat *iformat);
    void stream_close(VideoState *is);
    void stream_component_close(int stream_index);

    int video_open();

    void seek_chapter(int incr);

    void toggle_full_screen();
    void toggle_mute();

    void init_clock(Clock *c, int *queue_serial);

    // double get_master_clock();
    // int get_master_sync_type();
    void check_external_clock_speed();
    void set_clock_speed(Clock *c, double speed);

    void update_volume(int sign, double step);
    void update_video_pts(double pts, int serial);

    double compute_target_delay(double delay);
    double vp_duration(Frame *vp, Frame *nextvp);

    void video_refresh(double *remaining_time);
    void video_display();
    void video_audio_display();
    void video_image_display();
    int upload_texture(SDL_Texture **tex, AVFrame *frame);
    int realloc_texture(SDL_Texture **texture, Uint32 new_format,
                        int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);
    void get_sdl_pix_fmt_and_blendmode(int format, Uint32 *sdl_pix_fmt,
                                       SDL_BlendMode *sdl_blendmode);
    void set_sdl_yuv_conversion_mode(AVFrame *frame);
    inline void fill_rectangle(int x, int y, int w, int h);
    inline int compute_mod(int a, int b);


    static VideoCtl* m_pInstance;

    bool m_initIndex;
    
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_RendererInfo renderer_info = { 0 };
    SDL_AudioDeviceID audio_dev;
    WId play_wid; // 播放窗口

    int screen_width;
    int screen_height;
    int startup_volume;

    //播放刷新循环线程
    std::thread m_tPlayLoopThread;

    int m_frameW;
    int m_frameH;

signals:
    void SigPlayMsg(QString strMsg);                                   //< 错误信息
    void SigFrameDimensionsChanged(int nFrameWidth, int nFrameHeight); //<视频宽高发生变化

    void SigVideoTotalSeconds(int nSeconds);
    void SigVideoPlaySeconds(int nSeconds);

    void SigVideoVolume(double dPercent);
    void SigPauseStat(bool bPaused);

    void SigStop();
    void SigStopAndNext();

    void SigStopFinished(); // 停止播放完成

    void SigStartPlay(QString strFileName);

public slots:
    void OnPlaySeek(double dPercent);
    void OnPlayVolume(double dPercent);
    void OnPause();
    void OnStop();
    void OnSeekForward();
    void OnSeekBack();
    void OnAddVolume();
    void OnSubVolume();
};

#endif