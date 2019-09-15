#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <atomic>

class SpinLock
{
public:
    SpinLock(const SpinLock &) = delete;
    SpinLock& operator=(const SpinLock &) = delete;

    void lock() {
        while (isLocked());
    }

    void unlock() {
        m_lock.clear();
    }

private:
    bool isLocked() {
        return m_lock.test_and_set();
    }

    std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
};

#endif
