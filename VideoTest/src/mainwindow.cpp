#include "mainwindow.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <QApplication>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QMimeData>
#include <QPushButton>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QDebug>

VideoDecoder::VideoDecoder(QObject *parent)
    : QThread (parent)
{

}

VideoDecoder::~VideoDecoder()
{
    stop();
}

void VideoDecoder::stop()
{
    //必须先重置信号量
    m_frameQueue.init();
    m_runnable = false;
    wait();
}

void VideoDecoder::open(const QString &filename)
{
    stop();

    m_mutex.lock();
    m_filename = filename;
    m_runnable = true;
    m_mutex.unlock();

    start();
}

QImage VideoDecoder::currentFrame()
{
    static QImage image = QImage();
    image = m_frameQueue.tryDequeue();

    return image;
}

void VideoDecoder::run()
{
    demuxing_decoding();
}

void VideoDecoder::demuxing_decoding()
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
    videoIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    if (videoIndex >= 0)
        videoStream = formatContext->streams[videoIndex];

    if (videoStream)
        videoDecoder = avcodec_find_decoder(videoStream->codecpar->codec_id);

    if (videoDecoder)
        codecContext = avcodec_alloc_context3(videoDecoder);

    if (codecContext)
        avcodec_parameters_to_context(codecContext, videoStream->codecpar);

    if (codecContext)
        avcodec_open2(codecContext, videoDecoder, nullptr);

    //打印相关信息
    av_dump_format(formatContext, 0, "format", 0);
    fflush(stderr);

    m_fps = videoStream->r_frame_rate.num / videoStream->r_frame_rate.den;
    m_width = codecContext->width;
    m_height = codecContext->height;

    emit resolved();

    SwsContext *swsContext = sws_getContext(m_width, m_height, codecContext->pix_fmt, m_width, m_height, AV_PIX_FMT_RGB24,
                                            SWS_BILINEAR, nullptr, nullptr, nullptr);
    //分配并初始化一个临时的帧和包
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
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
                else if (ret < 0) return;

                int dst_linesize[4];
                uint8_t *dst_data[4];
                av_image_alloc(dst_data, dst_linesize, m_width, m_height, AV_PIX_FMT_RGB24, 1);
                sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);
                QImage image = QImage(dst_data[0], m_width, m_height, QImage::Format_RGB888).copy();
                av_freep(&dst_data[0]);

                m_frameQueue.enqueue(image);

                av_frame_unref(frame);
            }
        }

        av_packet_unref(packet);
    }

    m_fps = m_width = m_height = 0;

    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    if (swsContext) sws_freeContext(swsContext);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);
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
    m_decoder = new VideoDecoder(this);
    connect(m_decoder, &VideoDecoder::resolved, this, [this]() {
        QSize size = (qApp->primaryScreen()->size() - QSize(m_decoder->width(), m_decoder->height())) / 2;
        setGeometry(pos().x(), size.height(), m_decoder->width(), m_decoder->height());
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

