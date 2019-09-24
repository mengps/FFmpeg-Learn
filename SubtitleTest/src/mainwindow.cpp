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
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMimeData>
#include <QPushButton>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QDebug>

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

void SubtitleDecoder::setFilter(SubtitleDecoder::Filter filter)
{

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

void SubtitleDecoder::demuxing_decoding_video()
{
    AVFormatContext *formatContext = nullptr;
    AVCodecContext *codecContext = nullptr;
    AVCodec *videoDecoder = nullptr;
    AVStream *videoStream = nullptr;
    int videoIndex = -1;

    //打开输入文件，并分配格式上下文
    avformat_open_input(&formatContext, m_filename.toStdString().c_str(), nullptr, nullptr);
    avformat_find_stream_info(formatContext, nullptr);

    //找到视频流的索引
    for (size_t i = 0; i < formatContext->nb_streams; ++i) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIndex = int(i);
            videoStream = formatContext->streams[i];
            break;
        }
    }

    if (!videoStream) {
        qDebug() << "Has Error: line =" << __LINE__;
        return;
    }
    videoDecoder = avcodec_find_decoder(videoStream->codecpar->codec_id);

    if (!videoDecoder) {
        qDebug() << "Has Error: line =" << __LINE__;
        return;
    }
    codecContext = avcodec_alloc_context3(videoDecoder);

    if (!codecContext) {
        qDebug() << "Has Error: line =" << __LINE__;
        return;
    }
    avcodec_parameters_to_context(codecContext, videoStream->codecpar);

    if (!codecContext) {
        qDebug() << "Has Error: line =" << __LINE__;
        return;
    }
    avcodec_open2(codecContext, videoDecoder, nullptr);

    m_fps = videoStream->avg_frame_rate.num / videoStream->avg_frame_rate.den;
    m_width = codecContext->width;
    m_height = codecContext->height;

    //打印相关信息，在stderr
    av_dump_format(formatContext, 0, "format", 0);
    fflush(stderr);

    //初始化filter相关
    AVRational time_base = videoStream->time_base;
    QString args = QString::asprintf("video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                                    m_width, m_height, codecContext->pix_fmt, time_base.num, time_base.den,
                                    codecContext->sample_aspect_ratio.num, codecContext->sample_aspect_ratio.den);
    qDebug() << "Video Args: " << args;

    AVFilterContext *buffersrcContext = nullptr;
    AVFilterContext *buffersinkContext = nullptr;

    //字幕相关，使用subtitles，目前测试的是ass，但srt, ssa, ass, lrc都行，改后缀名即可
    int suffixLength = QFileInfo(m_filename).suffix().length();
    bool subtitleOpened = false;
    QString subtitleFilename = m_filename.mid(0, m_filename.length() - suffixLength - 1) + ".ass";

    //判断视频文件目录是否存在ass格式字幕，存在则拷贝到当前目录
    //因为ffmpeg subtiltes filter不支持路径
    if (QFile::exists(subtitleFilename)) {
        //拷贝到当前目录(exe路径)
        QString completeBaseName = QFileInfo(m_filename).completeBaseName() + ".ass";
        QFile::copy(subtitleFilename, "./" + completeBaseName);

        //初始化subtitle filter
        QString filterDesc = QString("subtitles=filename='%1':original_size=%2x%3").arg(completeBaseName).arg(m_width).arg(m_height);
        subtitleOpened = init_subtitle_filter(buffersrcContext, buffersinkContext, args, filterDesc);
        if (!subtitleOpened) {
            qDebug() << "字幕打开失败!";
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
            int ret = avcodec_send_packet(codecContext, packet);

            while (ret >= 0) {
                //从解码器接收解码后的帧
                ret = avcodec_receive_frame(codecContext, frame);

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

                        int dst_linesize[4];
                        uint8_t *dst_data[4];
                        av_image_alloc(dst_data, dst_linesize, m_width, m_height, AV_PIX_FMT_RGB24, 1);
                        SwsContext *swsContext = sws_getContext(filter_frame->width, filter_frame->height,
                                                                AVPixelFormat(filter_frame->format), m_width,
                                                                m_height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
                        sws_scale(swsContext, filter_frame->data, filter_frame->linesize, 0, filter_frame->height, dst_data, dst_linesize);
                        sws_freeContext(swsContext);
                        QImage image = QImage(dst_data[0], m_width, m_height, QImage::Format_RGB888).copy();
                        av_freep(&dst_data[0]);

                        m_frameQueue.enqueue(image);
                        av_frame_unref(filter_frame);
                    }
                } else {
                    //未找到字幕，直接输出图像
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    else if (ret < 0) return;

                    int dst_linesize[4];
                    uint8_t *dst_data[4];
                    av_image_alloc(dst_data, dst_linesize, m_width, m_height, AV_PIX_FMT_RGB24, 1);
                    SwsContext *swsContext = sws_getContext(m_width, m_height, codecContext->pix_fmt, m_width, m_height, AV_PIX_FMT_RGB24,
                                                            SWS_BILINEAR, nullptr, nullptr, nullptr);
                    sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);
                    QImage image = QImage(dst_data[0], m_width, m_height, QImage::Format_RGB888).copy();
                    av_freep(&dst_data[0]);

                    m_frameQueue.enqueue(image);

                }
                av_frame_unref(frame);
            }
        }

        av_packet_unref(packet);
    }

Run_End:
    if (packet) av_packet_free(&packet);
    if (formatContext) avformat_close_input(&formatContext);
    if (codecContext) avcodec_free_context(&codecContext);
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
        int w = m_decoder->width() > 900 ? 900 : m_decoder->width();
        int h = m_decoder->height() > 600 ? 600 : m_decoder->height();
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
    QPainter painter(this);
    if (!m_currentFrame.isNull())
        painter.drawImage(rect(), m_currentFrame);
    else {
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
