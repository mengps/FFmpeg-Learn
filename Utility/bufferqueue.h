#ifndef BUFFERQUEUE_H
#define BUFFERQUEUE_H

#ifdef DEBUG_OUTPUT
#include <iostream>
#endif

#include "semaphore.h"
#include <vector>

template <class T> class BufferQueue
{
public:
    BufferQueue(int bufferSize = 100) {
        setBufferSize(bufferSize);
    }

    ~BufferQueue() {
        init();
        std::vector<T>().swap(m_bufferQueue);
    }

    void setBufferSize(int bufferSize) {
        m_bufferSize = bufferSize;
        m_bufferQueue = std::vector<T>(bufferSize);
        m_useableSpace.acquire(m_useableSpace.available());
        m_freeSpace.release(m_bufferSize - m_freeSpace.available());
        m_front = m_rear = 0;
    }

    void enqueue(const T &element) {
#ifdef DEBUG_OUTPUT
        std::cout << "[freespace " << m_freeSpace.available()
                  << "] --- [useablespace " << m_useableSpace.available() << "]" << std::endl;
#endif
        m_freeSpace.acquire();
        m_bufferQueue[m_front++ % m_bufferSize] = element;
        m_useableSpace.release();
    }

    T dequeue() {
#ifdef DEBUG_OUTPUT
        std::cout << "[freespace " << m_freeSpace.available()
                  << "] --- [useablespace " << m_useableSpace.available() << "]" << std::endl;
#endif
        m_useableSpace.acquire();
        T element = m_bufferQueue[m_rear++ % m_bufferSize];
        m_freeSpace.release();

        return element;
    }

    /**
     * @brief tryDequeue
     * @note 尝试获取一个元素，并且在失败时不会阻塞调用线程
     * @return 成功返回对应T元素，失败返回默认构造的T元素
     */
    T tryDequeue() {
        T element;
        bool success = m_useableSpace.tryAcquire();
        if (success) {
            element = m_bufferQueue[m_rear++ % m_bufferSize];
            m_freeSpace.release();
        }

        return element;
    }

    void init() {
        m_useableSpace.acquire(m_useableSpace.available());
        m_freeSpace.release(m_bufferSize - m_freeSpace.available());
        m_front.store(0);
        m_rear.store(0);
    }

private:
    //         -1               +1
    //   [free space] -> [useable space]
    Semaphore m_freeSpace{0};
    Semaphore m_useableSpace{100};
    std::atomic_int m_rear{0};
    std::atomic_int m_front{0};
    std::vector<T> m_bufferQueue;
    int m_bufferSize;
};

#endif
