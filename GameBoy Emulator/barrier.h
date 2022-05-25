#ifndef BARRIER_H
#define BARRIER_H

#include <mutex>
#include <condition_variable>

class Barrier{
public:
    Barrier(int threads)
        : thread_count(threads)
        , counter(0)
        , waiting(0)
    {}

    void wait() {
        std::unique_lock<std::mutex> lk(m);
        ++counter;
        ++waiting;
        cv.wait(lk, [&] {return counter >= thread_count;});
        cv.notify_one();
        --waiting;
        if (waiting == 0) {
            counter = 0;
        }
        lk.unlock();
    }

private:
    std::mutex m;
    std::condition_variable cv;
    int counter;
    int waiting;
    int thread_count;
};

#endif