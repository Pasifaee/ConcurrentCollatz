#include <utility>
#include <deque>
#include <future>
#include <unistd.h>
#include <sys/wait.h>

#include "teams.hpp"
#include "contest.hpp"
#include "collatz.hpp"

// isn't there a problem with global variables? I mean, what happens when there are multiple teams,
// and they're all using same variables?
std::mutex res_mut_TNT;
std::mutex cond_mut_TNT;
std::condition_variable cond_TNT;

void writeResTNT(uint64_t id, ContestResult& results, InfInt const & input, uint32_t& running) {
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
    cond_TNT.notify_one();
}

ContestResult TeamNewThreads::runContestImpl(ContestInput const & contestInput)
{
    std::cout << "Input: \n";
    for (int i = 0; i < contestInput.size(); i++) {
        std::cout << contestInput[i] << " ";
    }
    std::cout << "\n";

    //std::cout << "TeamNewThreads starting with getSize = " << getSize() << "\n";
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
                //std::cout << "running = " << running << " (main)\n";
                return running < getSize();
            });
            //std::cout << "unlocking cond_mut_TNT in main\n";
        }
        //std::cout << "Creating " << std::min(getSize() - running_now, to_create) << " new threads.\n";
        {
            //std::cout << "locking cond_mut_TNT in main\n";
            std::unique_lock<std::mutex> lock(cond_mut_TNT);
            running += std::min(getSize() - running_now, to_create);
            //std::cout << "unlocking cond_mut_TNT in main\n";
        }
        for (uint32_t i = 0; i < std::min(getSize() - running_now, to_create); i++) {
            threads[to_create - 1] = createThread(writeResTNT, to_create - 1, std::ref(r), contestInput[to_create - 1],
                                                  std::ref(running));
            to_create--;
        }
        //std::cout << "to_create = " << to_create << "\n";
    }
    for (uint32_t i = 0; i < contestInput.size(); i++) {
        threads[i].join();
    }
    // to be deleted
    /*std::cout << "Correct result: ";
    for (int i = 0; i < r.size(); i++) {
        std::cout << r[i] << " ";
    }
    std::cout << "\n";*/
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
    //std::cout << "work_per_thread = " << work_per_thread << ", additional_work = " << additional_work << "\n";
    uint64_t idx_start = 0, idx_stop = work_per_thread + (additional_work > 0 ? 1 : 0);
    if (additional_work > 0)
        additional_work--;
    std::thread threads[getSize()];
    uint64_t j = 0;
    while (idx_stop <= contestInput.size() && j < getSize()) {
        //std::cout << "idx_stop = " << idx_stop << "\n";
        threads[j] = createThread(writeResTCT, std::ref(r), contestInput, idx_start, idx_stop);
        idx_start = idx_stop;
        idx_stop += work_per_thread + (additional_work > 0 ? 1 : 0);
        if (additional_work > 0)
            additional_work--;
        j++;
    }
    //std::cout << "j = " << j << "\n";
    assert(j == getSize());
    for (uint32_t i = 0; i < getSize(); i++) {
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
        std::string input_str_tmp = input.toString();
        const char* input_str = input_str_tmp.c_str();
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
        //if (to_create % 100 == 0)
            //std::cout << "to_create = " << to_create << "\n";
    }
    //std::cout << "Exited while.\n";
    // not sure if the range of i below is correct
    for (uint32_t i = 0; i < std::min((uint32_t) contestInput.size(), getSize() - 1); i++) {
        readAndWriteResTNP(pipe_dsc[0], r);
    }

    close(pipe_dsc[1]);
    return r;
}

static long MAX_WRITE = PIPE_BUF / sizeof(std::pair<uint64_t, uint32_t>);

ContestResult TeamConstProcesses::runContest(ContestInput const & contestInput)
{
    //std::cout << "Team Const Processes starting\n" << "MAX_WRITE = " << MAX_WRITE << "\n";
    ContestResult r;
    r.resize(contestInput.size());
    uint32_t work_per_process = contestInput.size() / getSize();
    uint32_t additional_work = contestInput.size() - (work_per_process) * getSize();

    int pipe_send_dsc[getSize()][2]; // pipes for sending info to new_process'es
    int pipe_receive_dsc[2]; // pipe for receiving info from new_process'es
    char pipe_read_dsc_str[10], pipe_write_dsc_str[10];
    pipe(pipe_receive_dsc);
    sprintf(pipe_write_dsc_str, "%d", pipe_receive_dsc[1]);
    int idx = 0;
    for (uint32_t i = 0; i < getSize(); i++) {
        pipe(pipe_send_dsc[i]);
        sprintf(pipe_read_dsc_str, "%d", pipe_send_dsc[i][0]);
        //std::cout << "send pipe read dsc = " << pipe_send_dsc[i][0] << ", write dsc = " << pipe_send_dsc[i][1] << "\n";
        std::string id = std::to_string(i);
        char const *id_str = id.c_str();
        std::string work = std::to_string(work_per_process + (i < additional_work ? 1 : 0));
        char const *work_str = work.c_str();

        //std::cout << "Writing input for the new_process (main)\n";
        for (int j = 0; j < std::min(work_per_process + (i < additional_work ? 1 : 0), (uint32_t) MAX_WRITE); j++) {
            std::string singleInput_str = contestInput[idx].toString();
            std::string idx_str = std::to_string(idx);
            std::string input_str = idx_str + '-' + singleInput_str;
            //std::cout << "Writing single input\n";
            //std::cout << "input = " << contestInput[idx] << ", idx = " << idx << ", input_str = " << input_str << "\n";
            //std::pair<uint64_t, uint32_t> input = std::make_pair(contestInput[idx].toLongLong(), idx);
            if (write(pipe_send_dsc[i][1], input_str.c_str(), input_str.size() + 1) != input_str.size() + 1) {
                std::cout << "Error in write\n";
                exit(-1);
            }
            idx++;
        }

        //std::cout << "Doing fork() (main)\n";
        switch (fork()) {
            case -1:
                std::cout << "Error in fork\n";
                exit(-1);

            case 0:
                //std::cout << "I'm a new process\n";
                //close(pipe_dsc[i][0][1]);
                //close(pipe_dsc[i][1][0]);
                execl("./new_process", "new_process", pipe_read_dsc_str, pipe_write_dsc_str, id_str, work_str, NULL);

            default:
                //close(pipe_dsc[i][0][0]);
                //close(pipe_dsc[i][1][1]);
                ;
        }
    }
    std::vector<long> work_left(getSize());
    for (uint32_t i = 0; i < getSize(); i++) {
        work_left[i] = std::max((long) 0, (long) work_per_process + (i < additional_work ? 1 : 0) - MAX_WRITE);
        //std::cout << "work_left[" << i << "] = " << work_left[i] << "\n";
    }

    for (uint32_t i = 0; i < contestInput.size(); i++) {
        std::tuple<uint64_t, uint32_t, int> single_result;
        //std::cout << "Reading single result ...\n";
        if (read(pipe_receive_dsc[0], &single_result, sizeof(single_result)) != sizeof(single_result)) {
            std::cout << "Error in read (main)\n";
            exit(-1);
        }
        uint64_t res = std::get<0>(single_result);
        uint32_t res_idx = std::get<1>(single_result);
        int process_id = std::get<2>(single_result);
        //std::cout << "Single result from process " << process_id << " equals " << res << "\n";
        if (work_left[process_id] > 0) {
            //std::cout << "New input: contestInput[idx] = " << contestInput[idx] << "\n";
            //std::pair<uint64_t, uint32_t> input = std::make_pair(contestInput[idx].toLongLong(), idx);
            //std::cout << "Writing input, idx = " << idx << "\n";
            std::string singleInput_str = contestInput[idx].toString();
            std::string idx_str = std::to_string(idx);
            std::string input_str = idx_str + '-' + singleInput_str;
            /*if (write(pipe_send_dsc[process_id][1], &input, sizeof(input)) != sizeof(input)) {
                std::cout << "Error in write (main)\n";
                exit(-1);
            }*/
            if (write(pipe_send_dsc[process_id][1], input_str.c_str(), input_str.size() + 1) != input_str.size() + 1) {
                std::cout << "Error in write\n";
                exit(-1);
            }
            work_left[process_id]--;
            idx++;
        }
        //std::cout << "Updating result to res_idx = " << res_idx << " ...";
        r[res_idx] = res;
        //std::cout << " updated\n";
    }
    for (uint32_t i = 0; i < getSize(); i++)
        wait(0);
    return r;
}

ContestResult TeamAsync::runContest(ContestInput const & contestInput)
{
    ContestResult r;
    std::vector<std::future<uint64_t>> results;
    for (auto singleInput : contestInput) {
        results.push_back(std::async(calcCollatz, singleInput));
    }
    for (auto &singleResult : results) {
        r.push_back(singleResult.get());
    }
    return r;
}
