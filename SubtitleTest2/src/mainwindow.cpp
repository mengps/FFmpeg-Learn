#include "mainwindow.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMimeData>
#include <QPushButton>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QTime>
#include <QDebug>

#ifdef MKTAG
#undef MKTAG
#define MKTAG(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | (unsigned(d) << 24))
#endif

//无警告版
#ifdef AVERROR_EOF
#undef AVERROR_EOF
#define AVERROR_EOF (-int(MKTAG('E', 'O', 'F', ' ')))
#endif

typedef const char * const_int8_ptr;

SubtitleDecoder::SubtitleDecoder(QObject *parent)
    : QThread (parent)
{

}

SubtitleDecoder::~SubtitleDecoder()
{
    stop();
}

void SubtitleDecoder::stop()
{
    //必须先重置信号量
    m_frameQueue.init();
    m_runnable = false;
    wait();
}

void SubtitleDecoder::open(const QString &filename)
{
    stop();

    m_mutex.lock();
    m_filename = filename;
    m_runnable = true;
    m_mutex.unlock();

    start();
}

QImage SubtitleDecoder::currentFrame()
{
    QImage image = m_frameQueue.tryDequeue();
    //Packet packet = m_frameQueue.tryDequeue();
    return image;
}

void SubtitleDecoder::run()
{
    demuxing_decoding_video();
}

bool SubtitleDecoder::init_subtitle_filter(AVFilterContext * &buffersrcContext, AVFilterContext * &buffersinkContext,
                                           QString args, QString filterDesc)
{
    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *output = avfilter_inout_alloc();
    AVFilterInOut *input = avfilter_inout_alloc();
    AVFilterGraph *filterGraph = avfilter_graph_alloc();

    auto release = [&output, &input] {
        avfilter_inout_free(&output);
        avfilter_inout_free(&input);
    };

    if (!output || !input || !filterGraph) {
        release();
        return false;
    }

    //创建输入过滤器，需要arg
    if (avfilter_graph_create_filter(&buffersrcContext, buffersrc, "in",
                                     args.toStdString().c_str(), nullptr, filterGraph) < 0) {
        qDebug() << "Has Error: line =" << __LINE__;
        release();
        return false;
    }

    if (avfilter_graph_create_filter(&buffersinkContext, buffersink, "out",
                                     nullptr, nullptr, filterGraph) < 0) {
        qDebug() << "Has Error: line =" << __LINE__;
        release();
        return false;
    }

    output->name = av_strdup("in");
    output->next = nullptr;
    output->pad_idx = 0;
    output->filter_ctx = buffersrcContext;

    input->name = av_strdup("out");
    input->next = nullptr;
    input->pad_idx = 0;
    input->filter_ctx = buffersinkContext;

    if (avfilter_graph_parse_ptr(filterGraph, filterDesc.toStdString().c_str(),
                                 &input, &output, nullptr) < 0) {
        qDebug() << "Has Error: line =" << __LINE__;
        release();
        return false;
    }

    if (avfilter_graph_config(filterGraph, nullptr) < 0) {
        qDebug() << "Has Error: line =" << __LINE__;
        release();
        return false;
    }

    release();
    return true;
}

bool SubtitleDecoder::open_codec_context(AVCodecContext *&context, AVStream *stream)
{
    AVCodec *dcoder = nullptr;

    if (!stream) {
        qDebug() << "Has Error: line =" << __LINE__;
        return false;
    }
    dcoder = avcodec_find_decoder(stream->codecpar->codec_id);

    if (!dcoder) {
        qDebug() << "Has Error: line =" << __LINE__;
        return false;
    }
    context = avcodec_alloc_context3(dcoder);

    if (!context) {
        qDebug() << "Has Error: line =" << __LINE__;
        return false;
    }
    avcodec_parameters_to_context(context, stream->codecpar);

    if (!context) {
        qDebug() << "Has Error: line =" << __LINE__;
        return false;
    }
    avcodec_open2(context, dcoder, nullptr);

    return true;
}

QImage SubtitleDecoder::convert_image(AVFrame *frame)
{
    int dst_linesize[4];
    uint8_t *dst_data[4];
    av_image_alloc(dst_data, dst_linesize, m_width, m_height, AV_PIX_FMT_RGB24, 1);
    SwsContext *swsContext = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format),
                                            m_width, m_height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);
    sws_freeContext(swsContext);
    QImage image = QImage(dst_data[0], m_width, m_height, QImage::Format_RGB888).copy();
    av_freep(&dst_data[0]);

    return image;
}

void SubtitleDecoder::demuxing_decoding_video()
{
    AVFormatContext *formatContext = nullptr;
    AVCodecContext *videoCodecContext = nullptr, *subCodecContext = nullptr;
    AVStream *videoStream = nullptr, *subStream = nullptr;
    int videoIndex = -1, subIndex = -1;

    //打开输入文件，并分配格式上下文
    avformat_open_input(&formatContext, m_filename.toStdString().c_str(), nullptr, nullptr);
    avformat_find_stream_info(formatContext, nullptr);

    //找到视频流，字幕流的索引
    for (size_t i = 0; i < formatContext->nb_streams; ++i) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIndex = int(i);
            videoStream = formatContext->streams[i];
        } else if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            subIndex = int(i);
            subStream = formatContext->streams[i];
        }
    }

    //打印相关信息，在 stderr
    av_dump_format(formatContext, 0, "format", 0);
    fflush(stderr);

    if (!open_codec_context(videoCodecContext, videoStream)) {
        qDebug() << "Open Video Context Failed!";
        return;
    }

    if (!open_codec_context(subCodecContext, subStream)) {
        //字幕流打开失败，也可能是没有，但无影响，接着处理
        qDebug() << "Open Subtitle Context Failed!";
    }

    m_fps = videoStream->avg_frame_rate.num / videoStream->avg_frame_rate.den;
    m_width = videoCodecContext->width;
    m_height = videoCodecContext->height;

    //初始化filter相关
    AVRational time_base = videoStream->time_base;
    QString args = QString::asprintf("video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                                    m_width, m_height, videoCodecContext->pix_fmt, time_base.num, time_base.den,
                                    videoCodecContext->sample_aspect_ratio.num, videoCodecContext->sample_aspect_ratio.den);
    qDebug() << "Video Args: " << args;

    AVFilterContext *buffersrcContext = nullptr;
    AVFilterContext *buffersinkContext = nullptr;
    bool subtitleOpened = false;

    //如果有字幕流
    if (subCodecContext) {
        //字幕流直接用视频名即可
        QString subtitleFilename = m_filename;
        subtitleFilename.replace('/', "\\\\");
        subtitleFilename.insert(subtitleFilename.indexOf(":\\"), char('\\'));
        QString filterDesc = QString("subtitles=filename='%1':original_size=%2x%3")
                .arg(subtitleFilename).arg(m_width).arg(m_height);
        qDebug() << "Filter Description:" << filterDesc.toStdString().c_str();
        subtitleOpened = init_subtitle_filter(buffersrcContext, buffersinkContext, args, filterDesc);
        if (!subtitleOpened) {
            qDebug() << "字幕打开失败!";
        }
    } else {
        //没有字幕流时，在同目录下寻找字幕文件
        //字幕相关，使用subtitles，目前测试的是ass，但srt, ssa, ass, lrc都行，改后缀名即可
        int suffixLength = QFileInfo(m_filename).suffix().length();
        QString subtitleFilename = m_filename.mid(0, m_filename.length() - suffixLength - 1) + ".ass";
        if (QFile::exists(subtitleFilename)) {
            //初始化subtitle filter
            //绝对路径必须转成D\:\\xxx\\test.ass这种形式, 记住，是[D\:\\]这种形式
            //toNativeSeparator()无用，因为只是 / -> \ 的转换
            subtitleFilename.replace('/', "\\\\");
            subtitleFilename.insert(subtitleFilename.indexOf(":\\"), char('\\'));
            QString filterDesc = QString("subtitles=filename='%1':original_size=%2x%3")
                    .arg(subtitleFilename).arg(m_width).arg(m_height);
            qDebug() << "Filter Description:" << filterDesc.toStdString().c_str();
            subtitleOpened = init_subtitle_filter(buffersrcContext, buffersinkContext, args, filterDesc);
            if (!subtitleOpened) {
                qDebug() << "字幕打开失败!";
            }
        }
    }

    emit resolved();

    //分配并初始化一个临时的帧和包
    AVPacket *packet = av_packet_alloc();;
    AVFrame *frame = av_frame_alloc();
    AVFrame *filter_frame = av_frame_alloc();
    packet->data = nullptr;
    packet->size = 0;

    //读取下一帧
    while (m_runnable && av_read_frame(formatContext, packet) >= 0) {
        if (packet->stream_index == videoIndex) {
            //发送给解码器
            int ret = avcodec_send_packet(videoCodecContext, packet);

            while (ret >= 0) {
                //从解码器接收解码后的帧
                ret = avcodec_receive_frame(videoCodecContext, frame);

                frame->pts = frame->best_effort_timestamp;

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) goto Run_End;

                //如果字幕成功打开，则输出使用subtitle filter过滤后的图像
                if (subtitleOpened) {
                    if (av_buffersrc_add_frame_flags(buffersrcContext, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
                        break;

                    while (true) {
                        ret = av_buffersink_get_frame(buffersinkContext, filter_frame);

                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                        else if (ret < 0) goto Run_End;

                        m_frameQueue.enqueue(convert_image(filter_frame));
                        av_frame_unref(filter_frame);
                    }
                } else {
                    //未找到字幕，直接输出图像
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    else if (ret < 0) goto Run_End;

                    m_frameQueue.enqueue(convert_image(frame));
                }
                av_frame_unref(frame);
            }
        } else if (packet->stream_index == subIndex) {
            AVSubtitle subtitle;
            int got_frame;
            int ret = avcodec_decode_subtitle2(subCodecContext, &subtitle, &got_frame, packet);

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            else if (ret < 0) goto Run_End;

            if (got_frame > 0) {
                //如果是图像字幕，即sub + idx
                //实际上，只需要处理这种即可
                if (subtitle.format == 0) {
                    for (size_t i = 0; i < subtitle.num_rects; i++) {
                        //AVSubtitleRect *sub_rect = subtitle.rects[i];
                    }
                } else {
                    //如果是文本格式字幕:srt, ssa, ass, lrc
                    //可以直接输出文本，实际上已经添加到过滤器中
                    qreal pts = packet->pts * av_q2d(subStream->time_base);
                    qreal duration = packet->duration * av_q2d(subStream->time_base);
                    const char *text = const_int8_ptr(packet->data);
                    qDebug() << "[PTS: " << pts << "]" << endl
                             << "[Duration: " << duration << "]" << endl
                             << "[Text: " << text << "]" << endl;
                }
            }
        }

        av_packet_unref(packet);
    }

Run_End:
    if (packet) av_packet_free(&packet);
    if (formatContext) avformat_close_input(&formatContext);
    if (videoCodecContext) avcodec_free_context(&videoCodecContext);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    QSize size = (qApp->primaryScreen()->size() - QSize(600, 500)) / 2;
    setGeometry(size.width(), size.height(), 600, 500);
    setAcceptDrops(true);

    QWidget *widget = new QWidget(this);
    widget->setFixedSize(200, 50);
    QHBoxLayout *layout = new QHBoxLayout(widget);
    m_suspendButton = new QPushButton("暂停");
    m_resumeButton = new QPushButton("继续");
    m_suspendButton->setFixedHeight(40);
    m_resumeButton->setFixedHeight(40);
    connect(m_suspendButton, &QPushButton::clicked, this, [this]() {
        m_timer->stop();
    });
    connect(m_resumeButton, &QPushButton::clicked, this, [this]() {
        m_timer->start();
    });
    layout->addWidget(m_suspendButton);
    layout->addWidget(m_resumeButton);
    widget->setLayout(layout);
    setCentralWidget(widget);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this](){
        m_currentFrame = m_decoder->currentFrame();
        update();
    });
    m_decoder = new SubtitleDecoder(this);
    connect(m_decoder, &SubtitleDecoder::resolved, this, [this]() {
        //限制大小900x600，并保持纵横比缩放
        qreal aspect = m_decoder->width() / qreal(m_decoder->height());
        int w = m_decoder->width(), h = m_decoder->height();
        if (w > 900) {
            w = 900;
            h = int(900.0 / aspect);
        } else if (h > 600) {
            h = 600;
            w = int(600.0 * aspect);
        }
        QSize size = (qApp->primaryScreen()->size() - QSize(w, h)) / 2;
        setGeometry(pos().x(), size.height(), w, h);
        m_timer->start(1000 / m_decoder->fps());
    });
}

MainWindow::~MainWindow()
{

}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    static bool running = false;

    QPainter painter(this);
    if (!m_currentFrame.isNull()) {
        if (!running) {
            running = true;
        }
        painter.drawImage(rect(), m_currentFrame);
    }
    else {
        if (running) {
            running = false;
            if (m_timer->isActive()) m_timer->stop();
        }
        QString text("<请拖入视频>");
        QFont f = font();
        f.setPointSize(20);
        painter.setFont(f);
        painter.setPen(Qt::red);
        int textWidth = painter.fontMetrics().horizontalAdvance(text);
        painter.drawText(width() / 2 - textWidth / 2, height() / 2, text);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    m_timer->stop();
    const QMimeData *mimeData = event->mimeData();
    if(mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        m_decoder->open(urlList[0].toLocalFile());
    }
}
