#include <utility>
#include <deque>
#include <future>
#include <unistd.h>
#include <sys/wait.h>

#include "teams.hpp"
#include "contest.hpp"
#include "collatz.hpp"

// isn't there a problem with global variables? i mean, what happens when there are multiple teams
// and they're all using same variables?
std::mutex res_mut_TNT;
std::mutex cond_mut_TNT;
std::condition_variable cond_TNT;

void writeResTNT(uint64_t id, ContestResult& results, InfInt const & input, uint32_t& running) {
    //std::cout << "memory = " << &running << " (thread)\n";
    uint64_t r = calcCollatz(input);
    //std::cout << "locking res_mut_TNT in thread " << id << "\n";
    res_mut_TNT.lock();
    results[id] = r;
    //std::cout << "unlocking res_mut_TNT in thread " << id << "\n";
    res_mut_TNT.unlock();
    {
        //std::cout << "locking cond_mut_TNT in thread " << id << "\n";
        //std::cout << "running = " << running << " (thread)\n";
        std::unique_lock<std::mutex> lock(cond_mut_TNT);
        running--;
        //std::cout << "unlocking cond_mut_TNT in thread " << id << "\n";
        //std::cout << "running = " << running << " (thread)\n";
    }
    //std::cout << "running = " << running << " (thread)\n";
    //std::cout << "memory = " << &running << " (thread)\n";
    cond_TNT.notify_one();
}

ContestResult TeamNewThreads::runContestImpl(ContestInput const & contestInput)
{
    assert(getSize() > 0);

    ContestResult r;
    r.resize(contestInput.size());

    uint32_t to_create = contestInput.size();
    uint32_t running = 0;
    std::thread threads[contestInput.size()];
    // Create min(to_create, getSize()) threads.
    for (int i = 0; i < std::min(to_create, getSize()); i++) {
        threads[to_create - 1] = createThread(writeResTNT, to_create - 1, std::ref(r),
                                              contestInput[to_create - 1], std::ref(running));
        to_create--;
        running++;
    }
    uint32_t running_now;
    //std::cout << "running = " << &running << " (main)\n";
    while (to_create > 0) {
        {
            //std::cout << "locking cond_mut_TNT in main\n";
            std::unique_lock<std::mutex> lock(cond_mut_TNT);
            cond_TNT.wait(lock, [this, &running, &running_now] {
                running_now = running;
                //std::cout << "memory = " << &running << " (main)\n";
                //std::cout << "running = " << running << " (main)\n";
                return running < getSize();
            });
            //std::cout << "unlocking cond_mut_TNT in main\n";
        }
        //std::cout << "Creating " << getSize() - running_now << " new threads.\n";
        for (uint32_t i = 0; i < getSize() - running_now; i++) {
            threads[to_create - 1] = createThread(writeResTNT, to_create - 1, std::ref(r), contestInput[to_create - 1],
                                                  std::ref(running));
            //std::cout << "memory = " << &running << " (main)\n";
            to_create--;
        }
        //std::cout << "to_create = " << to_create << "\n";
        {
            //std::cout << "locking cond_mut_TNT in main\n";
            std::unique_lock<std::mutex> lock(cond_mut_TNT);
            running += getSize() - running_now;
            //std::cout << "unlocking cond_mut_TNT in main\n";
        }
    }
    for (uint32_t i = 0; i < contestInput.size(); i++) {
        threads[i].join();
    }
    // to be deleted
    std::cout << "Correct result: ";
    for (int i = 0; i < r.size(); i++) {
        std::cout << r[i] << " ";
    }
    std::cout << "\n";
    return r;
}

std::mutex res_mut_TCT;

void writeResTCT(ContestResult& results, ContestInput contestInput, uint64_t idx_start, uint64_t idx_stop) {
    for (uint64_t i = idx_start; i < idx_stop; i++) {
        uint64_t r = calcCollatz(contestInput[i]);
        res_mut_TCT.lock();
        results[i] = r;
        res_mut_TCT.unlock();
    }
}

ContestResult TeamConstThreads::runContestImpl(ContestInput const & contestInput)
{
    assert(getSize() > 0);

    ContestResult r;
    r.resize(contestInput.size());

    uint32_t work_per_thread = contestInput.size() / getSize();
    uint32_t additional_work = contestInput.size() - (work_per_thread) * getSize();
    uint64_t idx_start = 0, idx_stop = work_per_thread + additional_work;
    std::thread threads[getSize()];
    uint64_t j = 0;
    while (idx_stop <= contestInput.size()) {
        threads[j] = createThread(writeResTCT, std::ref(r), contestInput, idx_start, idx_stop);
        idx_start = idx_stop;
        idx_stop += work_per_thread;
        j++;
    }
    assert(j == getSize());
    for (uint64_t i = 0; i < getSize(); i++) {
        threads[i].join();
    }

    return r;
}

ContestResult TeamPool::runContest(ContestInput const & contestInput)
{
    ContestResult r;
    cxxpool::thread_pool pool{getSize()};
    std::vector<std::future<uint64_t>> results;
    for (uint32_t i = 0; i < contestInput.size(); i++) {
        results.push_back(pool.push(calcCollatz, contestInput[i]));
    }

    for (uint32_t i = 0; i < contestInput.size(); i++) {
        r.push_back(results[i].get());
    }

    return r;
}

void readAndWriteResTNP(int read_dsc, std::vector<uint64_t>& r) {
    //std::cout << "Waiting for one of the processes to end.\n";
    std::pair<uint64_t, uint32_t> result;
    //std::cout << "Reading from the pipe.\n";
    if (read(read_dsc, &result, sizeof(result)) != sizeof(result)) {
        std::cout << "Error in read\n";
        exit(-1);
    }
    r[result.second] = result.first;
    wait(0);
}

ContestResult TeamNewProcesses::runContest(ContestInput const & contestInput)
{
    ContestResult r;
    r.resize(contestInput.size());
    int pipe_dsc[2];
    char pipe_write_dsc_str[10];
    uint32_t to_create = contestInput.size();

    pipe(pipe_dsc);
    sprintf(pipe_write_dsc_str, "%d", pipe_dsc[1]);

    while (to_create > 0) {
        InfInt input = contestInput[to_create - 1];
        std::basic_string<char> input_basic_str = input.toString();
        const char* input_str = input_basic_str.c_str(); // not sure if it can be a local loop variable
        char idx[10];
        sprintf(idx, "%d", to_create - 1);

        switch (fork()) {
            case -1:
                std::cout << "Error in fork\n";
                exit(-1);

            case 0:
                close(pipe_dsc[0]);
                execl("./new_process", "new_process", pipe_write_dsc_str, input_str, idx, NULL);

            default:
                // what if child processes would take all the space in the pipe buffer?
                to_create--;
                if (contestInput.size() - to_create >= getSize()) {
                    readAndWriteResTNP(pipe_dsc[0], r);
                }
        }
        if (to_create % 100 == 0)
            std::cout << "to_create = " << to_create << "\n";
    }
    std::cout << "Exited while.\n";
    // not sure if the range of i below is correct
    for (uint32_t i = 0; i < std::min((uint32_t) contestInput.size(), getSize() - 1); i++) {
        readAndWriteResTNP(pipe_dsc[0], r);
    }

    close(pipe_dsc[1]);
    return r;
}

ContestResult TeamConstProcesses::runContest(ContestInput const & contestInput)
{
    std::cout << "Team Const Processes starting\n";
    ContestResult r;
    char const *isTCP = "1";
    uint32_t work_per_process = contestInput.size() / getSize();
    uint32_t additional_work = contestInput.size() - (work_per_process) * getSize();
    uint64_t idx_start = 0, idx_stop = work_per_process + additional_work;

    int pipe_dsc[getSize()][2][2]; // a pair of pipes for every created process
    char pipe_read_dsc_str[10], pipe_write_dsc_str[10];

    for (uint32_t i = 0; i < getSize(); i++) {
        pipe(pipe_dsc[i][0]); // pipe for sending info to new_process
        pipe(pipe_dsc[i][1]); // pipe for receiving info from new_process
        sprintf(pipe_read_dsc_str, "%d", pipe_dsc[i][0][0]);
        sprintf(pipe_write_dsc_str, "%d", pipe_dsc[i][1][1]);
        std::string work = std::to_string(idx_stop - idx_start);
        char const *work_str = work.c_str();

        /*std::cout << "Descriptors (main):\n";
        std::cout << "dsc[i][0][0] = " << pipe_dsc[i][0][0] << ", dsc[i][0][1] = " << pipe_dsc[i][0][1];
        std::cout << ", dsc[i][1][0] = " << pipe_dsc[i][1][0] << ", dsc[i][1][1] = " << pipe_dsc[i][1][1] << "\n";
        */
        std::cout << "Writing input for the new_process (main)\n";
        for (int j = idx_start; j < idx_stop; j++) {
            if (j % 100 == 0) std::cout << "j = " << j << "\n";
            std::pair<InfInt, uint32_t> input = std::make_pair(contestInput[j], j);
            if (write(pipe_dsc[i][0][1], &input, sizeof(input)) != sizeof(input)) {
                std::cout << "Error in write\n";
                exit(-1);
            }
        }

        /*std::vector<InfInt> input(contestInput.begin() + idx_start, contestInput.begin() + idx_stop);
        assert(sizeof(input) <= PIPE_BUF);
        if (write(pipe_dsc[i][0][1], &input, sizeof(input)) != sizeof(input)) {
            std::cout << "Error in write\n";
            exit(-1);
        }*/
        std::cout << "Doing fork() (main)\n";
        switch (fork()) {
            case -1:
                std::cout << "Error in fork\n";
                exit(-1);

            case 0:
                std::cout << "I'm a new process\n";
                //close(pipe_dsc[i][0][1]);
                //close(pipe_dsc[i][1][0]);
                execl("./new_process", "new_process", pipe_read_dsc_str, pipe_write_dsc_str, work_str, isTCP, NULL);

            default:
                //close(pipe_dsc[i][0][0]);
                //close(pipe_dsc[i][1][1]);
                ;
        }

        idx_start = idx_stop;
        idx_stop += work_per_process;
    }
    std::cout << "TCP result: ";
    for (uint32_t i = 0; i < getSize(); i++) {
        std::vector<uint64_t> partial_result;
        if (read(pipe_dsc[i][1][0], &partial_result, SSIZE_MAX) == -1) {
            std::cout << "Error in read (main), errno = " << errno << "\n";
            exit(-1);
        }
        for (int j = 0; j < partial_result.size(); j++) {
            r.push_back(partial_result[i]);
            std::cout << partial_result[i] << " ";
        }
    }
    std::cout << "\n";
    return r;
}

ContestResult TeamAsync::runContest(ContestInput const & contestInput)
{
    ContestResult r;
    //TODO
    return r;
}
