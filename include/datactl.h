/* 
* 数据控制头文件 (datactl.h)
* 核心职责：定义播放器核心数据结构与队列管理逻辑
* 设计要点：
* 1. 多级缓冲体系：数据包队列->解码队列->渲染队列
* 2. 线程安全模型：SDL互斥锁+条件变量实现生产者-消费者模型
* 3. 时钟同步体系：音频/视频/外部三级时钟，支持动态速率调整
*/

#ifndef DATACTL_H
#define DATACTL_H

//------------------------ 平台适配 ------------------------
// Windows平台COM初始化要求
#ifdef _windows_
#include <windows.h>
#include <objbase.h>  // 必须包含以支持DirectShow设备枚举
#endif

// Linux音频设备低延迟配置
#ifdef linux
#include <unistd.h>   // 提供usleep等系统调用
#endif

// C++11线程支持（原SDL线程逐步迁移）
#include <thread>     // 未来替换SDL线程的过渡设计

//------------------------ 基础库 --------------------------
// 标准C库精选头文件（按功能排序）
#include <inttypes.h> // 精确宽度整数类型
#include <math.h>     // 数学运算（音视频同步计算）
#include <limits.h>   // 极限值检测（溢出保护）
#include <signal.h>   // 信号处理（中断响应）
#include <stdint.h>   // 跨平台整型定义
#include <assert.h>   // 调试断言（关键路径检查）

// FFmpeg兼容性处理（旧版本适配）
#ifndef INT64_C
#define INT64_C(c) (c ## LL)    // 处理FFmpeg版本差异
#define UINT64_C(c) (c ## ULL)  // 确保64位常量正确性
#endif

// FFmpeg核心组件（按处理流程排序）
extern "C" {
#include "libavcodec/avcodec.h"  // 编解码核心

// 工具库（功能模块化分组）
#include "libavutil/avstring.h"   // 字符串处理
#include "libavutil/channel_layout.h" // 声道布局
#include "libavutil/eval.h"       // 表达式解析（滤镜用）
#include "libavutil/mathematics.h"// 数学工具（时间基转换）
#include "libavutil/pixdesc.h"    // 像素格式描述
#include "libavutil/imgutils.h"   // 图像内存管理
#include "libavutil/dict.h"       // 元数据操作
#include "libavutil/fifo.h"       // 无锁队列实现
#include "libavutil/parseutils.h" // 参数解析
#include "libavutil/samplefmt.h"  // 采样格式
#include "libavutil/time.h"       // 高精度计时
#include "libavutil/bprint.h"     // 动态字符串
#include "libavformat/avformat.h" // 格式处理
#include "libavdevice/avdevice.h" // 设备输入输出
#include "libswscale/swscale.h"   // 图像缩放
#include "libavutil/opt.h"        // 参数选项
#include "libavutil/tx.h"         // 快速变换（FFT）
#include "libswresample/swresample.h" // 音频重采样

// 滤镜系统
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

// SDL2多媒体库（渲染核心）
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include "ffplay_renderer.h"
}


//----------------------- 全局常量 -------------------------
/* 队列容量设计原则：
* 视频队列：小容量+大帧体积（平衡内存与延迟）
* 音频队列：中容量+固定采样率（防卡顿）
* 字幕队列：大容量+时间轴复杂度（多语言支持）
*/
#define MAX_QUEUE_SIZE (15 * 1024 * 1024)  // 总内存警戒线（防止内存溢出）
#define MIN_FRAMES 25          // 最低解码帧数（保证seek后流畅）
#define EXTERNAL_CLOCK_MIN_FRAMES 2  // 外部时钟动态缓冲控制
#define EXTERNAL_CLOCK_MAX_FRAMES 10

// SDL音频参数（经验值调优）
#define SDL_AUDIO_MIN_BUFFER_SIZE 512  // 512样本=约11.6ms@44.1kHz
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30 // 避免频繁回调消耗CPU

// 音量调节参数（对数刻度）
#define SDL_VOLUME_STEP (0.75) // 每步0.75dB，共42级（0-128）

// 音频波形采样分析
#define SAMPLE_ARRAY_SIZE (8 * 65536) // 524k样本≈12秒@44.1kHz

// 同步阈值（动态抖动补偿）
#define AV_SYNC_THRESHOLD_MIN 0.04    // 40ms内不调整（人耳不敏感区）
#define AV_SYNC_THRESHOLD_MAX 0.1     // 超过100ms触发同步
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1// 长帧不补偿（避免连续丢帧）
#define AV_NOSYNC_THRESHOLD 10.0      // 差异过大放弃同步（直接跳帧）

// 外部时钟速率调整（直播流适配）
#define EXTERNAL_CLOCK_SPEED_MIN  0.900  // 最低90%速率
#define EXTERNAL_CLOCK_SPEED_MAX  1.010  // 最高101%速率
#define EXTERNAL_CLOCK_SPEED_STEP 0.001  // 每次调整0.1%

// 音频采样率补偿（变速不变调）
#define SAMPLE_CORRECTION_PERCENT_MAX 10 // ±10%速度调整
#define AUDIO_DIFF_AVG_NB   20           // 滑动平均窗口大小

// 渲染控制参数
#define REFRESH_RATE 0.01         // 10ms刷新间隔（100FPS）
#define CURSOR_HIDE_DELAY 1000000 // 光标隐藏延迟（1秒）
#define USE_ONEPASS_SUBTITLE_RENDER 1 // 字幕单次渲染优化

//----------------------- 数据结构 -------------------------
/* 数据包链表节点（内存优化设计）
* 采用AVFifo代替链表提升性能：
* 1. 内存连续访问（缓存友好）
* 2. 自动扩容机制（避免频繁分配）
* 3. 无锁设计潜力（未来优化方向）
*/
typedef struct MyAVPacketList {
    AVPacket* pkt;      // 数据包指针（引用计数管理）
    int serial;         // 序列号（防seek干扰）
} MyAVPacketList;

/* 数据包队列（生产者-消费者模型）
* 关键指标：
* - nb_packets：当前包数（流控依据）
* - size：总字节数（内存警戒）
* - duration：总时长（用于预缓冲计算）
*/
typedef struct PacketQueue {
    AVFifo* pkt_list;      // 环形缓冲区（FFmpeg实现）
    int nb_packets;        // 有效包数量
    int size;              // 队列总字节数
    int64_t duration;      // 队列总时长（单位：流时间基）
    int abort_request;     // 中止标志（原子操作）
    int serial;            // 当前队列版本号
    SDL_mutex* mutex;      // 互斥锁（关键区保护）
    SDL_cond* cond;        // 条件变量（线程唤醒）
} PacketQueue;

// 媒体类型帧队列容量（经验值）
#define VIDEO_PICTURE_QUEUE_SIZE 3  // 1080p每帧约6MB，3帧≈18MB
#define SUBPICTURE_QUEUE_SIZE 16    // 支持复杂字幕时间轴
#define SAMPLE_QUEUE_SIZE 9         // 200-800ms音频缓冲
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

/* 音频参数集（格式转换关键参数）
* 典型工作流程：
* 解码格式 -> 滤镜处理 -> 重采样 -> 目标格式
*/
typedef struct AudioParams {
    int freq;                // 采样率（如44100）
    AVChannelLayout ch_layout; // 声道布局（FFmpeg新版API）
    enum AVSampleFormat fmt; // 采样格式（如AV_SAMPLE_FMT_FLTP）
    int frame_size;          // 单帧采样数（如1024）
    int bytes_per_sec;       // 码率计算（freq * channels * bytes_per_sample）
} AudioParams;

/* 播放时钟体系（多时钟源同步）
* 实现策略：
* - 音频主时钟：优先保证连续性
* - 视频从时钟：动态丢帧追赶
* - 外部时钟：网络流同步
*/
typedef struct Clock {
    double pts;           // 基准时间（秒）
    double pts_drift;     // 与系统时钟的漂移（pts - gettime）
    double last_updated;  // 最后更新时间（系统时间）
    double speed;         // 播放速率（支持变速）
    int serial;           // 时钟版本号（防seek干扰）
    int paused;           // 暂停状态
    int *queue_serial;    // 关联队列版本号（跨线程同步）
} Clock;

// 帧元数据（调试与定位）
typedef struct FrameData {
    int64_t pkt_pos;      // 原始文件偏移（用于错误定位）
} FrameData;

/* 通用媒体帧容器（视频/音频/字幕）
* 内存管理策略：
* - 视频帧：AVFrame引用计数
* - 字幕：动态分配结构体
* - 音频：连续缓冲区
*/
typedef struct Frame {
    AVFrame* frame;       // 解码帧（视频/音频）
    AVSubtitle sub;       // 字幕数据
    int serial;           // 序列号（与队列版本一致）
    double pts;           // 显示时间戳（秒）
    double duration;      // 帧持续时间（秒）
    int64_t pos;          // 文件偏移（字节）
    int width;            // 图像宽度
    int height;           // 图像高度
    int format;           // 像素/采样格式
    AVRational sar;       // 像素宽高比（如16:9）
    int uploaded;         // GPU上传标记（避免重复提交）
    int flip_v;           // 垂直翻转标记（某些编码格式需要）
} Frame;

/* 帧队列（环形缓冲区实现）
* 设计要点：
* - 读写索引分离（无锁访问潜力）
* - 条件变量唤醒（精确线程控制）
* - 容量动态调整（根据媒体类型）
*/
typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE]; // 固定容量数组
    int rindex;         // 读位置（消费者）
    int windex;         // 写位置（生产者）
    int size;           // 当前帧数
    int max_size;       // 最大容量（初始化设定）
    int keep_last;      // 保留最后一帧（用于暂停）
    int rindex_shown;   // 读位置显示状态
    SDL_mutex* mutex;   // 互斥锁
    SDL_cond* cond;     // 条件变量
    PacketQueue* pktq;  // 关联数据包队列
} FrameQueue;

// 同步模式枚举（主时钟选择）
enum {
    AV_SYNC_AUDIO_MASTER,  // 默认模式（音频连续）
    AV_SYNC_VIDEO_MASTER,  // 无音频时使用
    AV_SYNC_EXTERNAL_CLOCK // 网络流/外部输入
};

/* 解码器控制结构（状态机管理）
* 生命周期：
* 初始化->启动->运行->暂停->刷新->销毁
*/
typedef struct Decoder {
    AVPacket* pkt;              // 当前处理包
    PacketQueue* queue;         // 输入队列
    AVCodecContext* avctx;      // 解码器上下文
    int pkt_serial;             // 当前包序列号
    int finished;               // 结束标记
    int packet_pending;         // 包暂存标记
    SDL_cond* empty_queue_cond; // 队列空信号
    int64_t start_pts;          // 初始时间戳
    AVRational start_pts_tb;    // 初始时间基
    int64_t next_pts;           // 预测时间戳
    AVRational next_pts_tb;     // 预测时间基
    SDL_Thread *decode_thread;  // 解码线程
} Decoder;

/* 全局播放状态机（核心控制结构）
* - 线程控制：解复用/解码/渲染线程管理
* - 媒体容器：格式探测/流选择
* - 播放控制：暂停/跳转/速率控制
* - 同步系统：时钟树管理
* - 子系统：音/视/字独立管理
*/
typedef struct VideoState {
    // 线程控制
    SDL_Thread *read_tid;        // 解复用线程
    int abort_request;           // 全局中止标志
    SDL_cond *continue_read_thread; // 读线程暂停控制

    // 媒体容器
    AVFormatContext *ic;         // 格式上下文
    const AVInputFormat *iformat;// 输入格式
    char *filename;              // 文件路径
    int realtime;                // 实时流标记
    int eof;                     // 文件结束标记
    int64_t start_time;          // 起始时间（用于循环播放）

    // 播放控制
    int paused;                  // 暂停状态
    int last_paused;             // 前次暂停状态（状态恢复用）
    int force_refresh;           // 强制重绘标记
    int step;                    // 单帧步进模式
    int seek_req;                // 跳转请求
    int64_t seek_pos;            // 跳转目标（微秒）
    int64_t seek_rel;            // 相对跳转量
    int seek_flags;              // 跳转标志（AVSEEK_FLAG_*）
    int queue_attachments_req;   // 附件处理请求（封面图）

    // 同步系统
    Clock audclk;                // 音频时钟
    Clock vidclk;                // 视频时钟
    Clock extclk;                // 外部时钟
    int av_sync_type;            // 当前同步模式
    double max_frame_duration;   // 最大帧间隔（跳帧检测）

    // 音频子系统
    struct {
        Decoder auddec;          // 音频解码器
        PacketQueue audioq;      // 音频包队列
        AVStream *audio_st;      // 音频流
        FrameQueue sampq;        // 采样队列

        AudioParams audio_src;   // 原始参数
        AudioParams audio_tgt;   // 目标参数
        AudioParams audio_filter_src; // 滤镜参数

        struct SwrContext *swr_ctx; // 重采样上下文
        uint8_t *audio_buf;      // 输出缓冲区
        uint8_t *audio_buf1;     // 备用缓冲区
        unsigned int audio_buf_size; // 缓冲区大小
        unsigned int audio_buf1_size;
        int audio_buf_index;     // 当前缓冲位置
        int audio_write_buf_size;// 待写入大小

        double audio_diff_cum;   // 差异累计
        double audio_diff_avg_coef; // 滑动平均系数
        double audio_diff_threshold;// 同步阈值
        int audio_diff_avg_count; // 平均计数器
        int audio_hw_buf_size;   // 硬件缓冲大小
        int audio_volume;        // 当前音量
        int muted;               // 静音状态
    } audio;

    // 视频子系统
    struct {
        Decoder viddec;          // 视频解码器
        PacketQueue videoq;      // 视频包队列
        AVStream *video_st;      // 视频流
        FrameQueue pictq;        // 图像队列

        struct SwsContext *sub_convert_ctx; // 字幕转换
        struct SwsContext *img_convert_ctx; // 图像转换
        AVRational sar;          // 像素宽高比
        int frame_drops_early;   // 主动丢帧计数
        int frame_drops_late;    // 延迟丢帧计数

        SDL_Texture *vid_texture;// 视频纹理
        double frame_timer;      // 帧计时器
        double frame_last_returned_time; // 最后显示时间
        double frame_last_filter_delay; // 滤镜延迟
        int width, height;       // 帧尺寸
        int format;              // 像素格式
        int flip_v;              // 垂直翻转
    } video;

    // 字幕子系统
    struct {
        Decoder subdec;          // 字幕解码器
        PacketQueue subtitleq;   // 字幕队列
        AVStream *subtitle_st;   // 字幕流
        FrameQueue subpq;        // 字幕帧队列
        SDL_Texture *sub_texture;// 字幕纹理
        int width, height;       // 字幕尺寸
    } subtitle;

    // 可视化系统
    enum ShowMode {
        SHOW_MODE_NONE = -1,     // 无可视化
        SHOW_MODE_VIDEO = 0,     // 视频模式
        SHOW_MODE_WAVES,         // 波形图
        SHOW_MODE_RDFT,          // 频谱图
        SHOW_MODE_NB
    } show_mode;                 // 当前显示模式

    struct {
        int16_t sample_array[SAMPLE_ARRAY_SIZE]; // 采样缓存
        int sample_array_index;  // 采样索引
        AVTXContext *rdft;       // FFT变换
        av_tx_fn rdft_fn;        // 变换函数
        AVComplexFloat *rdft_data; // 频谱数据
        float *real_data;        // 实数缓存
        SDL_Texture *vis_texture;// 可视化纹理
        int rdft_bits;           // FFT位数
        int xpos;                // 绘制位置
        double last_vis_time;    // 最后更新时间
        int last_i_start;
    } vis;

    // 滤镜系统
    AVFilterGraph *agraph;       // 音频滤镜图
    AVFilterContext *in_audio_filter;  // 音频输入滤镜
    AVFilterContext *out_audio_filter; // 音频输出滤镜
    AVFilterContext *in_video_filter;  // 视频输入滤镜
    AVFilterContext *out_video_filter; // 视频输出滤镜
    int vfilter_idx;             // 当前视频滤镜索引

    // 窗口管理
    int width, height;           // 窗口尺寸
    int xleft, ytop;             // 渲染偏移
    SDL_Texture *sub_texture;    // 独立字幕层

    // 流管理
    int video_stream;            // 当前视频流索引
    int audio_stream;            // 当前音频流索引
    int subtitle_stream;         // 当前字幕流索引
    int last_video_stream;       // 前次视频流
    int last_audio_stream;       // 前次音频流
    int last_subtitle_stream;    // 前次字幕流

    // 新增状态
    int read_pause_return;       // 读线程暂停返回值
    int frame_drops_late;        // 延迟丢帧计数
    double audio_clock;          // 当前音频时钟
    int audio_clock_serial;      // 音频时钟序列号
} VideoState;

/* 用户配置选项与运行时状态管理 */

//====================== 用户输入参数 ======================
/* 输入源配置 */
static const AVInputFormat *file_iformat; // 强制输入格式（如rtsp/udp）
static const char *input_filename;        // 输入文件/URL路径
static const char *window_title;          // 窗口标题（默认显示文件名）

/* 解码控制 */
static const char *video_codec_name;      // 指定视频解码器（如h264_cuvid）
static const char *audio_codec_name;      // 指定音频解码器（如aac_at）
static const char *subtitle_codec_name;   // 指定字幕解码器
static const char *wanted_stream_spec[AVMEDIA_TYPE_NB] = {0}; // 流选择过滤器（语法："v:0,a:2"）

/* 硬件加速 */
static const char *hwaccel;               // 硬件解码器类型（如"cuda"/"dxva2"）
static int enable_vulkan;                 // 启用Vulkan渲染器（实验性）
static char *vulkan_params;               // Vulkan渲染参数（JSON格式）

//====================== 播放控制参数 ======================
/* 基础控制 */
static int seek_by_bytes = -1;            // 按字节跳转（-1=自动，1=强制用于无时间戳流）
static float seek_interval = 10;          // 方向键跳转步长（秒）
static int loop = 1;                      // 循环播放次数（0=单次，1=无限循环）
static int autorotate = 1;                // 自动旋转视频（基于元数据）

/* 同步控制 */
static int av_sync_type = AV_SYNC_AUDIO_MASTER; // 主时钟源（默认音频同步）
static int framedrop = -1;                // 丢帧策略（-1=自动，0=禁用，1=允许丢帧）
static int infinite_buffer = -1;          // 无限缓冲（网络流防卡顿）

/* 性能参数 */
static int lowres = 0;                    // 低分辨率解码（0=关闭，1=1/2，2=1/4）
static int genpts = 0;                    // 生成缺失的PTS（修复损坏流）
static int filter_nbthreads = 0;          // 滤镜线程数（0=自动）

//====================== 显示系统配置 ======================
/* 窗口属性 */
static int default_width  = 640;          // 默认窗口宽度（适应4:3分辨率）
static int default_height = 480;          // 默认窗口高度（VGA标准）
static int screen_width  = 0;             // 全屏模式宽度（0=自动检测）
static int screen_height = 0;             // 全屏模式高度
static int borderless;                    // 无边框窗口（1=启用）
static int alwaysontop;                   // 窗口置顶（1=启用）
static int screen_left = SDL_WINDOWPOS_CENTERED;    // 窗口左边距
static int screen_top = SDL_WINDOWPOS_CENTERED;     // 窗口上边距

/* 渲染控制 */
static enum VideoState::ShowMode show_mode = VideoState::ShowMode::SHOW_MODE_NONE; // 可视化模式（波形/频谱）
static double rdftspeed = 0.02;           // 频谱更新速度（秒/帧）
static int display_disable;               // 禁用视频渲染（纯音频模式）

//====================== 音频/视频控制 ======================
/* 开关控制 */
static int audio_disable;                 // 禁用音频解码（1=静音模式）
static int video_disable;                 // 禁用视频解码（1=纯音频模式）
static int subtitle_disable;              // 禁用字幕渲染

/* 音量控制 */
static int startup_volume = 100;          // 初始音量（0-100线性，映射到SDL的0-128）

//====================== 交互控制 ======================
static int exit_on_keydown;               // 按键退出（1=任意键退出）
static int exit_on_mousedown;             // 鼠标点击退出（1=启用）
static int show_status = -1;              // 显示播放状态（-1=自动，0=禁用）
static int cursor_hidden = 0;             // 光标隐藏状态（1=隐藏）
static int64_t cursor_last_shown;         // 最后光标活动时间（用于自动隐藏）

//====================== 滤镜系统 ======================
static const char **vfilters_list = NULL; // 视频滤镜链（如"scale=1280:720"）
static int nb_vfilters = 0;               // 视频滤镜数量
static char *afilters = NULL;             // 音频滤镜描述（如"aresample=48000"）

//====================== 运行时状态 ======================
/* SDL上下文 */
static SDL_Window *window;                // 主窗口对象
static SDL_Renderer *renderer;            // 2D渲染器（软件/OpenGL）
static SDL_AudioDeviceID audio_dev;       // 音频设备ID
static SDL_RendererInfo renderer_info = {0}; // 渲染器能力信息

/* 硬件渲染 */
static VkRenderer *vk_renderer;           // Vulkan渲染器上下文

/* 播放状态 */
static int is_full_screen;                // 全屏状态标志
static int64_t audio_callback_time;       // 最后音频回调时间（用于延迟计算）

/* 补充 */
static int64_t start_time = AV_NOPTS_VALUE;     // 开始时间
static int64_t duration = AV_NOPTS_VALUE;       // 播放时长
static int autoexit;
static int fast = 0;    //加速解码
static int find_stream_info = 1;

//====================== 跨模块事件 ======================
#define FF_QUIT_EVENT (SDL_USEREVENT + 2) // 自定义退出事件（线程间通信）

//====================== 像素格式映射表 ======================
/* FFmpeg与SDL像素格式转换表
* 设计要点：
* 1. 覆盖常见YUV和RGB格式
* 2. 处理endian差异（NE宏处理字节序）
* 3. 特殊格式映射（如IYUV对应YUV420P）
*/
static const struct TextureFormatEntry {
    enum AVPixelFormat format;   // FFmpeg像素格式
    int texture_fmt;             // SDL纹理格式常量
} sdl_texture_format_map[] = {
    { AV_PIX_FMT_RGB8,           SDL_PIXELFORMAT_RGB332 },  // 8位调色板模式
    { AV_PIX_FMT_RGB444,         SDL_PIXELFORMAT_RGB444 },  // 12bit RGB
    { AV_PIX_FMT_RGB565,         SDL_PIXELFORMAT_RGB565 },  // 16bit RGB（常用）
    { AV_PIX_FMT_RGB24,          SDL_PIXELFORMAT_RGB24 },   // 24bit打包RGB
    { AV_PIX_FMT_0RGB32,         SDL_PIXELFORMAT_RGB888 },  // 32bit带对齐RGB
    { AV_PIX_FMT_YUV420P,        SDL_PIXELFORMAT_IYUV },    // YUV420平面格式
    { AV_PIX_FMT_YUYV422,        SDL_PIXELFORMAT_YUY2 },    // YUYV打包格式
    { AV_PIX_FMT_UYVY422,        SDL_PIXELFORMAT_UYVY },    // UYVY打包格式
    { AV_PIX_FMT_NONE,           SDL_PIXELFORMAT_UNKNOWN }, // 终止标记
};

static enum AVColorSpace sdl_supported_color_spaces[] = {
    AVCOL_SPC_BT709,
    AVCOL_SPC_BT470BG,
    AVCOL_SPC_SMPTE170M,
    AVCOL_SPC_UNSPECIFIED,
};


/* 数据包队列内部写入实现（线程安全需由外部锁保证） */
static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList pkt1;
    int ret;

    // 检查队列中止标志
    if (q->abort_request)
        return -1;  // 队列已中止，拒绝写入

    // 构造数据包节点
    pkt1.pkt = pkt;          // 引用传入的数据包
    pkt1.serial = q->serial; // 记录当前队列序列号

    // 写入FIFO缓冲区
    ret = av_fifo_write(q->pkt_list, &pkt1, 1);  // 写入1个元素到环形缓冲区
    if (ret < 0)
        return ret; // 返回FFmpeg错误码（通常为ENOMEM）

    // 更新队列统计指标
    q->nb_packets++;  // 数据包计数+1
    q->size += pkt1.pkt->size + sizeof(pkt1); // 内存占用增加（数据包+元数据）
    q->duration += pkt1.pkt->duration; // 累计时长（基于时间基）

    /* 特殊处理提示：DV格式需要深拷贝数据（当前未实现） */
    // 注：DV视频的每个包包含多个帧，直接引用可能引发问题

    // 唤醒等待线程
    SDL_CondSignal(q->cond);  // 触发条件变量通知消费者
    return 0;
}

/* 数据包入队公共接口（线程安全封装） */
static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacket* pkt1;             // 新数据包指针
    int ret;                    // 操作返回值

    /* 创建新数据包副本 */
    pkt1 = av_packet_alloc();   // 分配空AVPacket结构体
    if (!pkt1) {                // 内存分配失败处理
        av_packet_unref(pkt);   // 释放输入数据包引用
        return -1;              // 返回内存不足错误
    }
    av_packet_move_ref(pkt1, pkt);  // 转移数据包所有权（原pkt被置空）

    /* 临界区开始 */
    SDL_LockMutex(q->mutex);    // 获取队列互斥锁
    ret = packet_queue_put_private(q, pkt1);  // 调用内部写入方法
    SDL_UnlockMutex(q->mutex);  // 释放互斥锁

    /* 错误处理 */
    if (ret < 0)                // 内部写入失败
        av_packet_free(&pkt1);  // 释放新建的数据包

    return ret;                 // 返回操作结果
}

/* 空数据包入队接口（用于刷新解码器） */
static int packet_queue_put_nullpacket(PacketQueue* q, AVPacket* pkt, int stream_index)
{
    pkt->stream_index = stream_index;  // 设置目标流索引（视频/音频/字幕流）
    return packet_queue_put(q, pkt);   // 调用通用入队方法
}

/* 数据包队列初始化函数（线程安全基础设施准备） */
static int packet_queue_init(PacketQueue *q)
{
    // 清空队列结构体（避免残留数据）
    memset(q, 0, sizeof(PacketQueue));

    /* 创建自动扩容的FIFO缓冲区 */
    q->pkt_list = av_fifo_alloc2(
        1,                      // 初始容量（元素个数）
        sizeof(MyAVPacketList), // 每个元素大小
        AV_FIFO_FLAG_AUTO_GROW  // 自动扩容标志
        );
    if (!q->pkt_list)           // 内存分配失败处理
        return AVERROR(ENOMEM); // 返回FFmpeg内存错误码

    /* 初始化线程互斥锁 */
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {            // 创建失败处理
        av_log(NULL, AV_LOG_FATAL, "SDL互斥锁创建失败: %s\n", SDL_GetError());
        return AVERROR(ENOMEM); // 返回系统错误码
    }

    /* 初始化条件变量 */
    q->cond = SDL_CreateCond();
    if (!q->cond) {             // 创建失败处理
        av_log(NULL, AV_LOG_FATAL, "SDL条件变量创建失败: %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }

    /* 初始状态设置 */
    q->abort_request = 1;       // 初始为中止状态（需手动启动）
    return 0;                   // 返回成功状态
}

/* 数据包队列清空函数（线程安全的内存释放与状态重置） */
static void packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList pkt1;  // 临时存储从队列取出的数据包

    /* 临界区开始：加锁保证原子操作 */
    SDL_LockMutex(q->mutex);

    /* 循环释放所有数据包 */
    while (av_fifo_read(q->pkt_list, &pkt1, 1) >= 0) {
        av_packet_free(&pkt1.pkt);  // 释放AVPacket内存
    }

    /* 重置队列统计指标 */
    q->nb_packets = 0;    // 数据包计数器归零
    q->size = 0;          // 内存占用量归零
    q->duration = 0;      // 总时长归零

    /* 更新队列序列号（重要！）*/
    q->serial++;          // 使旧序列号的数据包失效

    /* 临界区结束：释放互斥锁 */
    SDL_UnlockMutex(q->mutex);
}

/* 数据包队列销毁函数（全资源释放与清理） */
static void packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q);
    av_fifo_freep2(&q->pkt_list);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

// 数据包队列中止请求（立即停止所有队列操作）
static void packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);    // 获取队列互斥锁

    q->abort_request = 1;       // 设置中止标志位（1=请求终止）

    SDL_CondSignal(q->cond);    // 触发条件变量唤醒等待线程

    SDL_UnlockMutex(q->mutex);  // 释放互斥锁
}

// 数据包队列启动/恢复（初始化队列工作状态）
static void packet_queue_start(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);    // 获取队列互斥锁
    q->abort_request = 0;       // 清除中止标志位（0=正常运作）
    q->serial++;                // 递增序列号使旧数据失效
    SDL_UnlockMutex(q->mutex);  // 释放互斥锁
}

/*
* 从数据包队列获取数据包（线程安全的阻塞/非阻塞操作）
* 返回值:
*   <0: 队列已中止
*    0: 无数据包且非阻塞模式
*   >0: 成功获取数据包
*/
static int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block, int* serial)
{
    MyAVPacketList pkt1; // 临时存储从队列取出的数据包节点
    int ret;             // 操作返回值

    SDL_LockMutex(q->mutex); // 进入临界区，加锁保证原子操作

    for (;;) {
        // 检查队列中止请求
        if (q->abort_request) {
            ret = -1;    // 设置中止返回值
            break;       // 退出循环
        }

        // 尝试从FIFO读取数据包节点
        if (av_fifo_read(q->pkt_list, &pkt1, 1) >= 0) {
            /* 成功读取数据包后的处理流程 */
            q->nb_packets--;  // 更新队列包计数器
            q->size -= pkt1.pkt->size + sizeof(pkt1); // 更新内存占用量
            q->duration -= pkt1.pkt->duration; // 更新总时长统计

            av_packet_move_ref(pkt, pkt1.pkt); // 转移数据包所有权（零拷贝）
            if (serial)
                *serial = pkt1.serial; // 返回数据包序列号
            av_packet_free(&pkt1.pkt); // 释放队列节点内存

            ret = 1;      // 设置成功返回值
            break;        // 退出循环
        }
        else if (!block) { // 非阻塞模式且无数据
            ret = 0;      // 设置无数据返回值
            break;        // 退出循环
        }
        else { // 阻塞模式且无数据可用
            /* 等待数据到达的条件变量 */
            SDL_CondWait(q->cond, q->mutex); // 释放锁并进入等待，唤醒时重新加锁
        }
    }

    SDL_UnlockMutex(q->mutex); // 退出临界区，释放互斥锁
    return ret; // 返回最终操作状态
}

/* 解码器初始化函数（资源绑定与状态准备） */
static int decoder_init(Decoder* d, AVCodecContext* avctx, PacketQueue* queue, SDL_cond* empty_queue_cond)
{
    /* 清零解码器控制结构体 */
    memset(d, 0, sizeof(Decoder));  // 确保所有字段初始化为0或NULL

    /* 创建解码用数据包容器 */
    d->pkt = av_packet_alloc();     // 分配AVPacket内存空间
    if (!d->pkt)                    // 内存分配失败处理
        return AVERROR(ENOMEM);     // 返回FFmpeg内存错误码

    /* 绑定解码器核心组件 */
    d->avctx = avctx;               // 关联FFmpeg解码器上下文
    d->queue = queue;               // 绑定输入数据包队列
    d->empty_queue_cond = empty_queue_cond; // 设置队列空条件变量

    /* 初始化时间戳相关参数 */
    d->start_pts = AV_NOPTS_VALUE;  // 初始化为无效时间戳（0x8000000000000000）
    d->pkt_serial = -1;             // 初始序列号设为无效值（防旧数据干扰）

    return 0;  // 返回初始化成功状态
}


/**
 * 控制视频帧显示时间戳(PTS)的重新排序策略，用于处理B帧导致的解码与显示顺序不一致问题。
 *
 * - 取值说明:
 *   -1: 自动模式，优先使用最佳推测时间戳(frame->best_effort_timestamp)，适用于存在B帧的流（默认）
 *    0: 禁用重排序，直接使用解码时间戳(DTS)，适用于无B帧的简单流
 *    1: 保留值(当前未使用)
 *
 * 该静态变量影响视频帧的时序处理逻辑，确保帧按正确顺序渲染。
 * 在解码器初始化时可能根据编码特性动态调整，当前默认-1自动适应复杂场景。
 */
static int decoder_reorder_pts = -1;

/**
 * 解码器核心帧处理函数 - 从数据包队列解码出媒体帧
 *
 * @param d     解码器上下文
 * @param frame 输出视频/音频帧容器
 * @param sub   输出字幕容器
 * @return >0: 成功获取帧
 *          0: 文件结束(EOF)
 *         <0: 错误或中止
 *
 * @算法流程:
 * 1. 双循环结构:
 *    - 外层循环：处理序列号变更和包获取
 *    - 内层循环：持续接收解码器输出帧
 *
 * 2. 时间戳处理策略:
 *    - 视频：根据decoder_reorder_pts选择最佳PTS
 *    - 音频：基于采样率的时间基转换
 *
 * 3. 序列号同步机制:
 *    - 检测数据包序列号变更时刷新解码器
 *    - 保证解码器状态与当前数据流一致
 */
static int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN); // 初始状态需要输入数据

    /*-------------------------------- 外层循环：处理数据包序列变更 --------------------------------*/
    for (;;) {
        /*>>>>>>>>>>>> 阶段1：尝试从解码器获取已解码帧 <<<<<<<<<<<<*/
        if (d->queue->serial == d->pkt_serial) { // 序列号匹配时才处理
            do {
                // 检查中止请求
                if (d->queue->abort_request)
                    return -1;

                /* 根据媒体类型接收解码帧 */
                switch (d->avctx->codec_type) {
                case AVMEDIA_TYPE_VIDEO:
                    ret = avcodec_receive_frame(d->avctx, frame);
                    if (ret >= 0) {
                        // 视频时间戳处理策略
                        if (decoder_reorder_pts == -1) {
                            frame->pts = frame->best_effort_timestamp; // 自动选择最佳PTS
                        }
                        else if (!decoder_reorder_pts) {
                            frame->pts = frame->pkt_dts; // 强制使用DTS
                        }
                    }
                    break;

                case AVMEDIA_TYPE_AUDIO:
                    ret = avcodec_receive_frame(d->avctx, frame);
                    if (ret >= 0) {
                        /* 音频时间基转换 (时间戳 -> 采样率时间基) */
                        AVRational tb = { 1, frame->sample_rate }; // 基于采样率的时间基
                        if (frame->pts != AV_NOPTS_VALUE) {
                            // 转换原始时间戳
                            frame->pts = av_rescale_q(frame->pts, d->avctx->pkt_timebase, tb);
                        }
                        else if (d->next_pts != AV_NOPTS_VALUE) {
                            // 无时间戳时推算时间戳
                            frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
                        }
                        // 更新下一帧预测时间戳
                        if (frame->pts != AV_NOPTS_VALUE) {
                            d->next_pts = frame->pts + frame->nb_samples; // 累加采样数
                            d->next_pts_tb = tb; // 保持时间基
                        }
                    }
                    break;
                }

                /* 处理解码结束状态 */
                if (ret == AVERROR_EOF) {
                    d->finished = d->pkt_serial; // 标记当前序列号已完成
                    avcodec_flush_buffers(d->avctx); // 清空解码器内部缓存
                    return 0; // 正常结束
                }

                // 成功获取解码帧立即返回
                if (ret >= 0)
                    return 1;

            } while (ret != AVERROR(EAGAIN)); // 持续获取直到需要新数据
        }

        /*>>>>>>>>>>>> 阶段2：获取新数据包送入解码器 <<<<<<<<<<<<*/
        do {
            // 当队列空时唤醒读取线程
            if (d->queue->nb_packets == 0)
                SDL_CondSignal(d->empty_queue_cond);

            // 处理待处理数据包标志
            if (d->packet_pending) {
                d->packet_pending = 0; // 清除挂起状态
            } else {
                /* 从队列获取新数据包 */
                int old_serial = d->pkt_serial;
                if (packet_queue_get(d->queue, d->pkt, 1, &d->pkt_serial) < 0)
                    return -1; // 队列中止

                /* 检测序列号变更 (如seek操作后) */
                if (old_serial != d->pkt_serial) {
                    avcodec_flush_buffers(d->avctx); // 刷新解码器
                    d->finished = 0; // 重置完成状态
                    d->next_pts = d->start_pts; // 恢复初始PTS
                    d->next_pts_tb = d->start_pts_tb; // 恢复初始时间基
                }
            }

            // 循环直到获取到当前序列号的数据包
            if (d->queue->serial == d->pkt_serial)
                break;

            av_packet_unref(d->pkt); // 丢弃过期序列号的数据包
        } while (1);

        /*>>>>>>>>>>>> 阶段3：处理不同类型数据包 <<<<<<<<<<<<*/
        if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            /* 字幕解码特殊处理 */
            int got_frame = 0;
            ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, d->pkt);
            if (ret < 0) {
                ret = AVERROR(EAGAIN); // 需要重试
            } else {
                // 处理零数据包结束符
                if (got_frame && !d->pkt->data) {
                    d->packet_pending = 1; // 标记需要后续处理
                }
                ret = got_frame ? 0 : (d->pkt->data ? AVERROR(EAGAIN) : AVERROR_EOF);
            }
            av_packet_unref(d->pkt); // 字幕解码后立即释放
        } else {
            if (d->pkt->buf && !d->pkt->opaque_ref) {
                FrameData *fd;

                d->pkt->opaque_ref = av_buffer_allocz(sizeof(*fd));
                if (!d->pkt->opaque_ref)
                    return AVERROR(ENOMEM);
                fd = (FrameData*)d->pkt->opaque_ref->data;
                fd->pkt_pos = d->pkt->pos;
            }

            /* 音视频数据包送入解码器 */
            if (avcodec_send_packet(d->avctx, d->pkt) == AVERROR(EAGAIN)) {
                // API异常状态处理：同时需要输入和输出
                av_log(d->avctx, AV_LOG_ERROR,
                    "解码器状态异常：同时需要输入和输出，可能造成数据积压\n");
                d->packet_pending = 1; // 保留数据包下次重试
            } else {
                av_packet_unref(d->pkt); // 正常情况释放数据包
            }
        }
    } // end for(;;)
}

//解码器销毁
static void decoder_destroy(Decoder *d) {
    av_packet_free(&d->pkt);
    avcodec_free_context(&d->avctx);
}

//释放帧队列中单个帧项持有的资源
static void frame_queue_unref_item(Frame *vp)
{
    if (vp->frame) { // 空指针检查
        av_frame_unref(vp->frame);
    }
    if (vp->sub.rects) { // 根据字幕文档规范检查
        avsubtitle_free(&vp->sub);
    }
}

/*------------------------------- 帧队列核心操作 -------------------------------*/

/**
 * 初始化帧队列结构
 * @param f         队列容器
 * @param pktq      关联的数据包队列(用于状态同步)
 * @param max_size  请求的最大容量(实际取FRAME_QUEUE_SIZE最小值)
 * @param keep_last 是否保留最后显示的帧(用于暂停时画面保持)
 * @return 0成功，AVERROR错误码失败
 *
 * @关键操作:
 * 1. 创建互斥锁和条件变量实现线程安全访问
 * 2. 预分配AVFrame内存避免解码时动态分配
 * 3. 使用!!keep_last确保标志位为0/1
 */
static int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last)
{
    int i;
    memset(f, 0, sizeof(FrameQueue));
    if (!(f->mutex = SDL_CreateMutex())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    if (!(f->cond = SDL_CreateCond())) {
        av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    f->keep_last = !!keep_last;
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}

/**
 * 销毁帧队列并释放所有资源
 * @param f 目标队列
 *
 * @资源释放顺序:
 * 1. 释放所有帧的引用和字幕数据
 * 2. 销毁AVFrame容器
 * 3. 销毁同步对象(mutex/cond)
 */
static void frame_queue_destroy(FrameQueue *f)
{
    int i;
    for (i = 0; i < f->max_size; i++) {
        Frame* vp = &f->queue[i];
        frame_queue_unref_item(vp);
        av_frame_free(&vp->frame);
    }
    SDL_DestroyMutex(f->mutex);
    SDL_DestroyCond(f->cond);
}

/**
 * 唤醒等待队列的线程
 * @使用场景:
 * - 队列状态变化时(如push/next操作后)
 * - 中止播放时需唤醒所有阻塞线程
 */
static void frame_queue_signal(FrameQueue *f)
{
    SDL_LockMutex(f->mutex);
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/*------------------------------- 帧访问操作 --------------------------------*/

/**
 * 获取当前可读帧(不移动读指针)
 * @return 当前应显示的帧指针
 */
static Frame* frame_queue_peek(FrameQueue* f)
{
    // 计算逻辑: (读索引 + 显示状态) % 容量
    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

/**
 * 获取下一可读帧(用于预加载)
 * @return 下一个待显示的帧指针
 */
static Frame* frame_queue_peek_next(FrameQueue* f)
{
    // 计算逻辑: (读索引 + 显示状态 + 1) % 容量
    return &f->queue[(f->rindex + f->rindex_shown + 1) % f->max_size];
}

/**
 * 获取最新入队的帧(用于特殊操作)
 * @return 最近写入的帧指针
 */
static Frame* frame_queue_peek_last(FrameQueue* f)
{
    return &f->queue[f->rindex];
}

/**
 * 获取可写帧位置(阻塞直到有空位)
 * @return 可写入的帧指针，NULL表示队列已中止
 *
 * @同步机制:
 * 1. 加锁检查队列大小
 * 2. 当队列满时条件等待
 * 3. 写索引循环使用环形缓冲区
 */
static Frame* frame_queue_peek_writable(FrameQueue* f)
{
    /* wait until we have space to put a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size >= f->max_size &&
        !f->pktq->abort_request) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[f->windex];
}

/**
 * 获取可读帧位置(阻塞直到有数据)
 * @return 可读取的帧指针，NULL表示队列已中止
 *
 * @注意 rindex_shown标志影响有效数据判断:
 * - 当keep_last=1时，rindex_shown=0表示有未显示帧
 */
static Frame* frame_queue_peek_readable(FrameQueue* f)
{
    /* wait until we have a readable a new frame */
    SDL_LockMutex(f->mutex);
    while (f->size - f->rindex_shown <= 0 &&
        !f->pktq->abort_request) {
        SDL_CondWait(f->cond, f->mutex);
    }
    SDL_UnlockMutex(f->mutex);

    if (f->pktq->abort_request)
        return NULL;

    return &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
}

/*------------------------------- 队列操作 --------------------------------*/

/**
 * 提交新帧到队列
 * @操作流程:
 * 1. 写索引循环递增
 * 2. 加锁更新队列大小
 * 3. 发送条件信号唤醒读线程
 */
static void frame_queue_push(FrameQueue* f)
{
    if (++f->windex == f->max_size)
        f->windex = 0;
    SDL_LockMutex(f->mutex);
    f->size++;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/**
 * 移动到下一帧(释放当前帧资源)
 * @特殊处理:
 * - 当keep_last=1时，首次调用仅标记rindex_shown
 * - 正常情况释放当前帧并移动读索引
 *
 * @资源管理:
 * 调用frame_queue_unref_item确保释放解码数据
 */
static void frame_queue_next(FrameQueue* f)
{
    if (f->keep_last && !f->rindex_shown) {
        f->rindex_shown = 1;
        return;
    }
    frame_queue_unref_item(&f->queue[f->rindex]);
    if (++f->rindex == f->max_size)
        f->rindex = 0;
    SDL_LockMutex(f->mutex);
    f->size--;
    SDL_CondSignal(f->cond);
    SDL_UnlockMutex(f->mutex);
}

/*------------------------------- 帧队列状态查询 ------------------------------*/

/**
 * 获取队列中待显示的帧数量
 *
 * @param f 帧队列指针
 * @return 未显示的帧数（含保留帧逻辑）
 *
 * @计算逻辑:
 * 总帧数(size) - 已显示标记(rindex_shown)
 * 当启用keep_last时：
 *   - rindex_shown=0 表示有1帧未显示
 *   - rindex_shown=1 表示无新帧
 * 例：size=3, rindex_shown=1 -> 3-1=2帧待显示
 */
static int frame_queue_nb_remaining(FrameQueue* f)
{
    return f->size - f->rindex_shown;
}

/**
 * 获取最后有效显示帧的文件位置
 *
 * @param f 帧队列指针
 * @return 文件偏移量(字节)，-1表示无效
 *
 * @校验条件:
 * 1. rindex_shown=1 确认帧已通过显示流程
 * 2. 帧序列号与队列一致 确保未发生seek操作
 *
 * @典型用途:
 * - 播放进度显示
 * - seek操作后恢复位置校验
 */
static int64_t frame_queue_last_pos(FrameQueue* f)
{
    Frame* fp = &f->queue[f->rindex];
    // 双重验证保证位置有效性
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1; // 无效位置标识
}

/*------------------------------- 解码器中止操作 ------------------------------*/

/**
 * 安全中止解码器及相关资源
 *
 * @param d  解码器上下文
 * @param fq 关联的帧队列
 *
 * @执行流程:
 * 1. 中止数据包队列（设置abort_request标志）
 * 2. 唤醒帧队列阻塞线程（防止死锁）
 * 3. 等待解码线程自然退出（避免强制终止）
 * 4. 清空队列残留数据包（释放内存）
 *
 * @注意要点:
 * - 必须先唤醒线程再等待，否则可能死锁
 * - 线程结束后才能安全flush队列
 * - 典型调用场景：播放停止、seek、销毁播放器
 */
static void decoder_abort(Decoder* d, FrameQueue* fq)
{
    // 步骤1：设置中止标志（触发队列获取函数退出）
    packet_queue_abort(d->queue);

    // 步骤2：唤醒可能阻塞在帧队列的线程
    frame_queue_signal(fq);

    // 步骤3：等待解码线程结束（防止资源泄漏）
    SDL_WaitThread(d->decode_thread, NULL);
    d->decode_thread = NULL;

    // 步骤4：清空未处理的数据包
    packet_queue_flush(d->queue);
}

//判断输入媒体源是否为实时流媒体
static int is_realtime(AVFormatContext *s)
{
    if(   !strcmp(s->iformat->name, "rtp")
        || !strcmp(s->iformat->name, "rtsp")
        || !strcmp(s->iformat->name, "sdp")
        )
        return 1;

    if(s->pb && (   !strncmp(s->url, "rtp:", 4)
                || !strncmp(s->url, "udp:", 4)
                )
        )
        return 1;
    return 0;
}

#endif // DATACTL_H
