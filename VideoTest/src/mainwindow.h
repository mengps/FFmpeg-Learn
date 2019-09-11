#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMutex>
#include <QQueue>
#include <QThread>

class VideoDecoder : public QThread
{
    Q_OBJECT

public:
    VideoDecoder(QObject *parent = nullptr);
    ~VideoDecoder();

    void stop();
    void open(const QString &filename);

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
    void semaphoreInit();
    void demuxing_decoding();

    bool m_runnable = true;
    QMutex m_mutex;
    QString m_filename;
    QQueue<QImage> m_frameQueue;
    int m_fps, m_width, m_height;
};

class QPushButton;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void needUpdate();

protected:
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QTimer *m_timer;
    QImage m_currentFrame;
    VideoDecoder *m_decoder;
    QPushButton *m_suspendButton;
    QPushButton *m_resumeButton;
};

#endif // MAINWINDOW_H
