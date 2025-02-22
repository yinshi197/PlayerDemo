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
    explicit VideoCtl(QObject *parent = nullptr);
    ~VideoCtl();

    void StartPlay(QString strFileName, WId widPlayWid);
    void StopPlay();

    static int audio_open(void *opaque, AVChannelLayout *wanted_channel_layout,
                          int wanted_sample_rate, struct AudioParams *audio_hw_params);

    static void sdl_audio_callback(void *opaque, Uint8 *stream, int len);
    static void set_clock_at(Clock *c, double pts, int serial, double time);
    static void sync_clock_to_slave(Clock *c, Clock *slave);
    static double get_clock(Clock *c);
    static void set_clock(Clock *c, double pts, int serial);

    static void update_sample_display(VideoState *is, short *samples, int samples_size);

    static int audio_decode_frame(VideoState *is);
    static int synchronize_audio(VideoState *is, int nb_samples);
    static double get_master_clock(VideoState *is);
    static int get_master_sync_type(VideoState *is);

    static int read_thread(void *arg, VideoCtl *videoCtl);
    static int decode_interrupt_cb(void *ctx);

    static int decoder_start(Decoder *d, int (*fn)(void *), const char *thread_name, void *arg);

    static void stream_seek(VideoState *is, int64_t pos, int64_t rel, int by_bytes);
    static void step_to_next_frame(VideoState *is);
    static void stream_toggle_pause(VideoState *is);
    static int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);

    static int stream_component_open(VideoState *is, int stream_index);

    static int audio_thread(void *arg);
    static inline int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                                     enum AVSampleFormat fmt2, int64_t channel_count2);
    static int configure_audio_filters(VideoState *is, const char *afilters, int force_output_format);

    static int video_thread(void *arg);
    static int get_video_frame(VideoState *is, AVFrame *frame);
    static int queue_picture(VideoState *is, AVFrame *src_frame,
                             double pts, double duration, int64_t pos, int serial);
    static void set_default_window_size(int width, int height, AVRational sar);
    static void calculate_display_rect(SDL_Rect *rect,
                                       int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                       int pic_width, int pic_height, AVRational pic_sar);
    static int configure_video_filters(AVFilterGraph *graph,
                                       VideoState *is, const char *vfilters, AVFrame *frame);
    static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
                                     AVFilterContext *source_ctx, AVFilterContext *sink_ctx);

    static int subtitle_thread(void *arg);

    static int filter_codec_opts(const AVDictionary *opts, enum AVCodecID codec_id,
                                 AVFormatContext *s, AVStream *st, const AVCodec *codec,
                                 AVDictionary **dst);

    static int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec);
    static int create_hwaccel(AVBufferRef **device_ctx);

    static void toggle_pause(VideoState *is);
    void do_exit();

    VideoState *m_CurVideoState = nullptr;
    
private:
    bool Init();
    void ConnectSig();

    void event_loop();
    void refresh_loop_wait_event(SDL_Event *event);

    void stream_open(const char *filename, const AVInputFormat *iformat);
    void stream_close();
    void stream_component_close(int stream_index);
    void stream_cycle_channel(int codec_type);
        
    int video_open();

    void seek_chapter(int incr);

    void toggle_full_screen();
    void toggle_audio_display();
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

    bool m_initIndex;     //
    bool m_playLoopIndex; //

    WId play_wid; // 播放窗口

    int m_frameW;
    int m_frameH;

signals:
    void SigVideoTotalSeconds(int nSeconds); 

};

#endif