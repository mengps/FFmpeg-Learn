#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "bufferqueue.h"

#include <QAudioFormat>
#include <QMainWindow>
#include <QMutex>
#include <QQueue>
#include <QThread>

struct Packet
{
    QImage data;
    qreal time;
};

class SubtitleDecoder : public QThread
{
    Q_OBJECT

public:
    SubtitleDecoder(QObject *parent = nullptr);
    ~SubtitleDecoder();

    void stop();
    void open(const QString &filename);

    int duration();
    int fps() const { return m_fps; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    QImage currentFrame();

signals:
    void resolved();
    void finish();

protected:
    void run();

private:
    void demuxing_decoding();

    qreal m_duration = 0.0;
    qreal m_currentTime = 0.0;
    bool m_runnable = true;
    QMutex m_mutex;
    QString m_filename;
    BufferQueue<Packet> m_frameQueue;
    int m_fps, m_width, m_height, m_subtitleCount = 0;
};

class QSlider;
class QAudioOutput;
class QPushButton;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void needUpdate();

protected:
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QTimer *m_timer = nullptr;
    QImage m_currentFrame = QImage();
    SubtitleDecoder *m_decoder = nullptr;
};

#endif // MAINWINDOW_H
