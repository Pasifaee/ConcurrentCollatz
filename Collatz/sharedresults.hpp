#ifndef SHAREDRESULTS_HPP
#define SHAREDRESULTS_HPP

class SharedResults
{
public:
    bool tryGetResult(InfInt n) {
        std::unique_lock<std::mutex> lock(cond_mut);
        std::map<InfInt, uint64_t>::iterator res = results.find(n);
        if (res == results.end())
            results[n] = 0;
        return res != results.end();
    }
    uint64_t getResult(InfInt n) {
        std::unique_lock<std::mutex> lock(cond_mut);
        cond.wait(lock, [this, n]{ return results[n] != 0; });
        return results[n];
    }
    void pushResult(InfInt n, uint64_t res) {
        std::unique_lock<std::mutex> lock(cond_mut);
        results[n] = res;
        lock.unlock();
        cond.notify_all();
    }

private:
    std::map<InfInt, uint64_t> results;
    std::mutex cond_mut;
    std::condition_variable cond;
};

#endif