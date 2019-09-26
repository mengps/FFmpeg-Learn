# FFmpeg-Learn

FFmpeg学习记录，用法记录，小例子

------
### 安装、配置 

 - 下载 Qt [Qt官网](https://www.qt.io/)

 - 下载 FFmpeg [FFmpeg官网](https://www.ffmpeg.org/)

------
### 小例子

 - VideoTest

```
   FFmpeg视频解码测试
```
 - AudioTest

```
   FFmpeg音频解码测试
```
 - SubtitleTest

```
   FFmpeg字幕解码测试，只支持外挂字幕

   使用FFmpeg Subtitle Filter，依赖于libass

   如果使用的是ffmpeg文件夹中的(version 4.2)，则无需额外编译
```
 - SubtitleTest2

```
   FFmpeg字幕解码测试升级版，支持外挂，内封，内嵌字幕
    
   内封字幕解码提供[sub + idx格式]、[ass格式]
```
------
### 关于Utility

 - BufferQueue

```
   使用信号量实现的缓沖队列(类似环形队列)
```
 - Semaphore

```
   使用c++11封装的信号量
```
 - SpinLock

```
   使用c++11封装的自旋锁
```
------

`注意` 仅仅是为了学习FFmpeg而编写，难免有很多不足之处，还望多多指正