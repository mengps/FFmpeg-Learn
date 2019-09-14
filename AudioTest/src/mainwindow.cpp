#include "mainwindow.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#include <QApplication>
#include <QAudioOutput>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QPainter>
#include <QScreen>
#include <QSlider>
#include <QTimer>
#include <QDebug>

AudioDecoder::AudioDecoder(QObject *parent)
    : QThread (parent)
{

}

AudioDecoder::~AudioDecoder()
{
    stop();
}

void AudioDecoder::stop()
{
    //必须先重置信号量
    m_frameQueue.init();
    QMutexLocker locker(&m_mutex);
    m_runnable = false;
    wait();
}

void AudioDecoder::open(const QString &filename)
{
    stop();

    m_mutex.lock();
    m_filename = filename;
    m_runnable = true;
    m_mutex.unlock();

    start();
}

QAudioFormat AudioDecoder::format()
{
    QMutexLocker locker(&m_mutex);
    return m_format;
}

QByteArray AudioDecoder::currentFrame()
{
    QByteArray data = QByteArray();
    Packet packet = m_frameQueue.tryDequeue();
    data += packet.data;
    if (packet.time >= m_duration) emit finish();

    return data;
}

void AudioDecoder::run()
{
    demuxing_decoding();
}

void AudioDecoder::demuxing_decoding()
{
    AVFormatContext *formatContext = nullptr;
    AVCodecContext *codecContext = nullptr;
    AVCodec *audioDecoder = nullptr;
    AVStream *audioStream = nullptr;
    int audioIndex = -1;

    //打开输入文件，并分配格式上下文
    avformat_open_input(&formatContext, m_filename.toStdString().c_str(), nullptr, nullptr);
    avformat_find_stream_info(formatContext, nullptr);

    //找到视频流的索引
    audioIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (audioIndex >= 0)
        audioStream = formatContext->streams[audioIndex];

    if (audioStream)
        audioDecoder = avcodec_find_decoder(audioStream->codecpar->codec_id);

    if (audioDecoder)
        codecContext = avcodec_alloc_context3(audioDecoder);

    if (codecContext)
        avcodec_parameters_to_context(codecContext, audioStream->codecpar);

    if (codecContext)
        avcodec_open2(codecContext, audioDecoder, nullptr);

    //打印相关信息
    av_dump_format(formatContext, 0, "format", 0);
    fflush(stderr);

    QAudioFormat format;
    format.setCodec("audio/pcm");
    format.setSampleRate(codecContext->sample_rate);
    format.setSampleType(QAudioFormat::SignedInt);
    format.setSampleSize(8 * av_get_bytes_per_sample(AV_SAMPLE_FMT_S32));
    format.setChannelCount(codecContext->channels);
    m_format = format;
    //av_seek_frame(formatContext, audioIndex, 8 * audioStream->time_base.den, AVSEEK_FLAG_ANY);
    m_duration = audioStream->duration * av_q2d(audioStream->time_base);

    emit resolved();

    SwrContext *swrContext = swr_alloc_set_opts(nullptr, int64_t(codecContext->channel_layout), AV_SAMPLE_FMT_S32, codecContext->sample_rate,
                                    int64_t(codecContext->channel_layout), codecContext->sample_fmt, codecContext->sample_rate,
                                    0, nullptr);
    swr_init(swrContext);

    //分配并初始化一个临时的帧和包
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    packet->data = nullptr;
    packet->size = 0;

    //读取下一帧
    while (m_runnable && av_read_frame(formatContext, packet) >= 0) {
        if (packet->stream_index == audioIndex) {
            //发送给解码器
            int ret = avcodec_send_packet(codecContext, packet);

            QByteArray data;
            while (ret >= 0) {
                //从解码器接收解码后的帧
                ret = avcodec_receive_frame(codecContext, frame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                else if (ret < 0) return;

                int size = av_samples_get_buffer_size(nullptr, frame->channels, frame->nb_samples, AV_SAMPLE_FMT_S32, 0);
                uint8_t *buf = new uint8_t[size];
                swr_convert(swrContext, &buf, frame->nb_samples, const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                data += QByteArray((const char *)buf, size);
                delete[] buf;

                qreal time = frame->pts * av_q2d(audioStream->time_base) + frame->pkt_duration * av_q2d(audioStream->time_base);
                m_currentTime = time;

                m_frameQueue.enqueue({ data, time });

                av_frame_unref(frame);
            }
        }

        av_packet_unref(packet);
    }

    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    if (swrContext) swr_free(&swrContext);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    QSize size = (qApp->primaryScreen()->size() - QSize(600, 380)) / 2;
    setGeometry(size.width(), size.height(), 600, 380);
    setAcceptDrops(true);

    QWidget *widget = new QWidget(this);
    widget->setFixedHeight(40);
    QHBoxLayout *layout = new QHBoxLayout;
    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setValue(0);
    m_volume->setMinimum(0);
    m_volume->setMaximum(100);
    m_volume->setFixedHeight(30);
    QLabel *volumeLabel = new QLabel(this);
    volumeLabel->setText(QString("当前音量: 0 / 100"));
    volumeLabel->setFixedHeight(30);
    connect(m_volume, &QSlider::valueChanged, this, [volumeLabel, this](int value) {
        volumeLabel->setText(QString("当前音量: %1 / 100").arg(value));
        if (m_output) m_output->setVolume(qreal(value) / m_volume->maximum());
    });
    m_suspendButton = new QPushButton("暂停");
    m_resumeButton = new QPushButton("继续");
    m_suspendButton->setFixedHeight(30);
    m_resumeButton->setFixedHeight(30);
    connect(m_suspendButton, &QPushButton::clicked, this, [this]() {
        m_timer->stop();
    });
    connect(m_resumeButton, &QPushButton::clicked, this, [this]() {
        m_timer->start();
    });
    layout->addWidget(volumeLabel);
    layout->addWidget(m_volume);
    layout->addWidget(m_suspendButton);
    layout->addWidget(m_resumeButton);
    QVBoxLayout *mainLayout = new QVBoxLayout(widget);
    mainLayout->addLayout(layout);
    widget->setLayout(mainLayout);
    widget->hide();
    setCentralWidget(widget);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this](){
        if (m_currentFrame.size() < m_output->bytesFree()) {
            m_currentFrame += m_decoder->currentFrame();
            int readSize = m_output->periodSize();
            int chunks = m_output->bytesFree() / readSize;
            while (chunks--) {
                QByteArray pcm = m_currentFrame.mid(0, readSize);
                int len = pcm.size();
                m_currentFrame.remove(0, len);

                if (len) m_device->write(pcm);
                if (len != readSize) break;
            }
        }
    });

    m_decoder = new AudioDecoder;
    connect(m_decoder, &AudioDecoder::resolved, this, [this]() {
        centralWidget()->show();
        if (m_output) m_output->deleteLater();
        m_output = new QAudioOutput(m_decoder->format());
        m_volume->setValue(int(qreal(m_volume->maximum()) * m_output->volume()));
        m_device = m_output->start();
        m_timer->start(10);
    });
    connect(m_decoder, &AudioDecoder::finish, this, [this]() {
        centralWidget()->hide();
    });
}

MainWindow::~MainWindow()
{
    m_decoder->stop();
    m_decoder->wait();
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    QString text("<请拖入音频或视频>");
    QFont f = font();
    f.setPointSize(20);
    painter.setFont(f);
    painter.setPen(Qt::red);
    int textWidth = painter.fontMetrics().horizontalAdvance(text);
    painter.drawText(width() / 2 - textWidth / 2, height() / 2, text);
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

