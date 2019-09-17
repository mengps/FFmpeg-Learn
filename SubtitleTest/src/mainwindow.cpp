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
    QMutexLocker locker(&m_mutex);
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
    static QImage image = QImage();
    Packet packet = m_frameQueue.tryDequeue();
    image = packet.data;
    qreal time = packet.time;

    return image;
}

void SubtitleDecoder::run()
{
    demuxing_decoding();
}

void SubtitleDecoder::demuxing_decoding()
{
    AVFormatContext *formatContext = nullptr;
    AVCodecContext *codecContext[32] = { nullptr };
    AVCodec *subtitleDecoder = nullptr;
    AVStream *subtitleStream[32] = { nullptr };
    int subtitleIndex[32] = { -1 };

    //打开输入文件，并分配格式上下文
    avformat_open_input(&formatContext, m_filename.toStdString().c_str(), nullptr, nullptr);
    avformat_find_stream_info(formatContext, nullptr);

    //找到视频流的索引
    for (size_t i = 0; i < formatContext->nb_streams; ++i) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            subtitleIndex[m_subtitleCount] = int(i);
            subtitleStream[m_subtitleCount] = formatContext->streams[i];
            m_subtitleCount++;
        }
    }

    if (m_subtitleCount == 0) {
        qDebug() << "无可用字幕流";
        avformat_close_input(&formatContext);
        return;
    }

    for (int i = 0; i < m_subtitleCount; ++i) {
        if (subtitleStream[i])
            subtitleDecoder = avcodec_find_decoder(subtitleStream[i]->codecpar->codec_id);

        if (subtitleDecoder)
            codecContext[i] = avcodec_alloc_context3(subtitleDecoder);

        if (codecContext[i])
            avcodec_parameters_to_context(codecContext[i], subtitleStream[i]->codecpar);

        if (codecContext[i])
            avcodec_open2(codecContext[i], subtitleDecoder, nullptr);
    }

    //打印相关信息
    av_dump_format(formatContext, 0, "format", 0);
    fflush(stderr);

    emit resolved();

    /*SwsContext *swsContext = sws_getContext(m_width, m_height, codecContext->pix_fmt, m_width, m_height, AV_PIX_FMT_RGB24,
                                            SWS_BILINEAR, nullptr, nullptr, nullptr);*/
    //分配并初始化一个临时的帧和包
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    packet->data = nullptr;
    packet->size = 0;
    AVSubtitle subtitle;

    //读取下一帧
    while (m_runnable && av_read_frame(formatContext, packet) >= 0) {

        for (int i = 0; i < m_subtitleCount; ++i) {
            if (packet->stream_index == subtitleIndex[i]) {

                int got_frame;
                int ret = avcodec_decode_subtitle2(codecContext[i], &subtitle, &got_frame, packet);

                if (ret >= 0) {
                    //从解码器接收解码后的帧

                    qDebug() << "subtitle";

                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    else if (ret < 0) return;

                    /*int dst_linesize[4];
                    uint8_t *dst_data[4];
                    av_image_alloc(dst_data, dst_linesize, m_width, m_height, AV_PIX_FMT_RGB24, 1);
                    sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);
                    QImage image = QImage(dst_data[0], m_width, m_height, QImage::Format_RGB888).copy();
                    av_freep(&dst_data[0]);*/

                }

                break;
            }
        }

        av_packet_unref(packet);
    }

    m_fps = m_width = m_height = m_subtitleCount = 0;

    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    //if (swsContext) sws_freeContext(swsContext);
    if (formatContext) avformat_close_input(&formatContext);
    for (int i = 0; i < m_subtitleCount; ++i) {
        if (codecContext[i]) avcodec_free_context(&codecContext[i]);
    }
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
    widget->setLayout(layout);
    setCentralWidget(widget);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this](){
        m_currentFrame = m_decoder->currentFrame();
        update();
    });
    m_decoder = new SubtitleDecoder(this);
    connect(m_decoder, &SubtitleDecoder::resolved, this, [this]() {
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

