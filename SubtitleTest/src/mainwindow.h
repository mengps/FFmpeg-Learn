#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "bufferqueue.h"

#include <QAudioFormat>
#include <QMainWindow>
#include <QMutex>
#include <QQueue>
#include <QThread>

class AVRational;
class AVFilterContext;
class SubtitleDecoder : public QThread
{
    Q_OBJECT

public:
    enum Filter {

    };

    SubtitleDecoder(QObject *parent = nullptr);
    ~SubtitleDecoder();

    void stop();
    int fps() const { return m_fps; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    void open(const QString &filename);
    void setFilter(Filter filter);

    QImage currentFrame();

signals:
    void resolved();
    void finish();

protected:
    void run();

private:
    bool init_subtitle_filter(AVFilterContext *&buffersrc, AVFilterContext *&buffersink,
                              QString args, QString filterDesc);
    void demuxing_decoding_video();

    bool m_runnable = true;
    QMutex m_mutex;
    QString m_filename;
    BufferQueue<QImage> m_frameQueue;
    int m_fps, m_width, m_height;
};

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
    QTimer *m_timer;
    QImage m_currentFrame;
    SubtitleDecoder *m_decoder;
    QPushButton *m_suspendButton;
    QPushButton *m_resumeButton;
};

#endif // MAINWINDOW_H
