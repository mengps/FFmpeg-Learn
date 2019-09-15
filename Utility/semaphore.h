#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <atomic>
#include <condition_variable>
#include <mutex>

class Semaphore
{
public:
    explicit Semaphore(int i = 0) {
        m_semaphore.store(i < 0 ? 0 : i);
    }

    Semaphore(const Semaphore &) = delete;
    Semaphore& operator=(const Semaphore &) = delete;

    void acquire(int i = 1) {
        if (i <= 0) return;

        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_semaphore.load() < i) {
            m_conditionVar.wait(lock);
        }
        m_semaphore.fetch_sub(i);
    }

    bool tryAcquire(int i = 1) {
        if (i <= 0) return false;

        if (m_semaphore.load() >= i) {
            m_semaphore.fetch_sub(i);
            return true;
        } else return false;
    }

    void release(int i = 1) {
        if (i <= 0) return;

        m_semaphore.fetch_add(i);
        m_conditionVar.notify_one();
    }

    int available() const {
        return m_semaphore.load();
    }

private:
    std::condition_variable m_conditionVar;
    std::atomic_int m_semaphore;
    std::mutex m_mutex;
};

#endif
