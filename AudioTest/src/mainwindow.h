#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAudioFormat>
#include <QMainWindow>
#include <QMutex>
#include <QQueue>
#include <QThread>

struct Packet
{
    QByteArray data;
    qreal time;
};

class AudioDecoder : public QThread
{
    Q_OBJECT

public:
    AudioDecoder(QObject *parent = nullptr);
    ~AudioDecoder();

    void stop();
    void open(const QString &filename);

    QAudioFormat format();
    int duration();
    QByteArray currentFrame();

signals:
    void resolved();
    void finish();

protected:
    void run();

private:
    void semaphoreInit();
    void demuxing_decoding();

    qreal m_duration = 0.0;
    qreal m_currentTime = 0.0;
    bool m_runnable = true;
    QAudioFormat m_format;
    QMutex m_mutex;
    QString m_filename;
    QQueue<Packet> m_frameQueue;
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
    QByteArray m_currentFrame = QByteArray();
    QAudioOutput *m_output = nullptr;
    QIODevice *m_device = nullptr;
    AudioDecoder *m_decoder = nullptr;
    QSlider *m_volume;
    QPushButton *m_suspendButton = nullptr;
    QPushButton *m_resumeButton = nullptr;
};

#endif // MAINWINDOW_H
