#include <utility>
#include <deque>
#include <future>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include "teams.hpp"
#include "contest.hpp"
#include "collatz.hpp"

std::mutex res_mut_TNT;
std::mutex cond_mut_TNT;
std::condition_variable cond_TNT;

uint64_t calcCollatzX(InfInt const & n, std::shared_ptr<SharedResults>& sharedResults) {
    assert(n > 0);
    if (n == 1) {
        return 0;
    }
    uint64_t res;
    if (sharedResults->tryGetResult(n)) {
        return sharedResults->getResult(n);
    }
    else {
        if (n % 2 == 0) {
            res = calcCollatzX(n / 2, sharedResults) + 1;
            sharedResults->pushResult(n, res);
            return res;
        }
        else {
            res = calcCollatzX(n * 3 + 1, sharedResults) + 1;
            sharedResults->pushResult(n, res);
            return res;
        }
    }
}

uint64_t calcCollatzX_processes(InfInt const & n, SharedResults* sharedResults) {
    assert(n > 0);
    if (n == 1) {
        return 0;
    }
    uint64_t res;
    if (sharedResults->tryGetResult(n)) {
        return sharedResults->getResult(n);
    }
    else {
        if (n % 2 == 0) {
            res = calcCollatzX_processes(n / 2, sharedResults) + 1;
            sharedResults->pushResult(n, res);
            return res;
        }
        else {
            res = calcCollatzX_processes(n * 3 + 1, sharedResults) + 1;
            sharedResults->pushResult(n, res);
            return res;
        }
    }
}

void writeResTNT(uint64_t id, ContestResult& results, InfInt const & input, uint32_t& running, std::shared_ptr<SharedResults> sharedResults) {
    uint64_t r;
    if (sharedResults)
        r = calcCollatzX(input, sharedResults);
    else
        r = calcCollatz(input);
    res_mut_TNT.lock();
    results[id] = r;
    res_mut_TNT.unlock();
    {
        std::unique_lock<std::mutex> lock(cond_mut_TNT);
        running--;
    }
    cond_TNT.notify_one();
}

ContestResult TeamNewThreads::runContestImpl(ContestInput const & contestInput)
{
    ContestResult r;
    r.resize(contestInput.size());

    uint32_t to_create = contestInput.size();
    uint32_t running = 0;
    std::thread threads[contestInput.size()];
    // Create min(to_create, getSize()) threads.
    for (int i = 0; i < std::min(to_create, getSize()); i++) {
        threads[to_create - 1] = createThread(writeResTNT, to_create - 1, std::ref(r),
                                              contestInput[to_create - 1], std::ref(running), getSharedResults());
        to_create--;
        running++;
    }
    uint32_t running_now;
    while (to_create > 0) {
        {
            std::unique_lock<std::mutex> lock(cond_mut_TNT);
            cond_TNT.wait(lock, [this, &running, &running_now] {
                running_now = running;
                return running < getSize();
            });
        }
        {
            std::unique_lock<std::mutex> lock(cond_mut_TNT);
            running += std::min(getSize() - running_now, to_create);
        }
        for (uint32_t i = 0; i < std::min(getSize() - running_now, to_create); i++) {
            threads[to_create - 1] = createThread(writeResTNT, to_create - 1, std::ref(r), contestInput[to_create - 1],
                                                  std::ref(running), getSharedResults());
            to_create--;
        }
    }
    for (uint32_t i = 0; i < contestInput.size(); i++) {
        threads[i].join();
    }
    return r;
}

std::mutex res_mut_TCT;

void writeResTCT(ContestResult& results, ContestInput contestInput, uint64_t idx_start, uint64_t idx_stop, std::shared_ptr<SharedResults> sharedResults) {
    for (uint64_t i = idx_start; i < idx_stop; i++) {
        uint64_t r;
        if (sharedResults) {
            r = calcCollatzX(contestInput[i], sharedResults);
        }
        else {
            r = calcCollatz(contestInput[i]);
        }
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
    uint64_t idx_start = 0, idx_stop = work_per_thread + (additional_work > 0 ? 1 : 0);
    if (additional_work > 0)
        additional_work--;
    std::thread threads[getSize()];
    uint64_t j = 0;
    while (idx_stop <= contestInput.size() && j < getSize()) {
        threads[j] = createThread(writeResTCT, std::ref(r), contestInput, idx_start, idx_stop, getSharedResults());
        idx_start = idx_stop;
        idx_stop += work_per_thread + (additional_work > 0 ? 1 : 0);
        if (additional_work > 0)
            additional_work--;
        j++;
    }
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
        if (getSharedResults()) {
            results.push_back(pool.push(calcCollatzX, contestInput[i], getSharedResults()));
        }
        else {
            results.push_back(pool.push(calcCollatz, contestInput[i]));
        }
    }

    for (uint32_t i = 0; i < contestInput.size(); i++) {
        r.push_back(results[i].get());
    }

    return r;
}

void readAndWriteResTNP(int read_dsc, std::vector<uint64_t>& r) {
    std::pair<uint64_t, uint32_t> result;
    if (read(read_dsc, &result, sizeof(result)) != sizeof(result)) {
        std::cout << "Error in read\n";
        exit(-1);
    }
    r[result.second] = result.first;
}

void new_process_TNP(int write_dsc, InfInt input, int idx, SharedResults* shR) {
    uint64_t result;
    if (shR) {
        result = calcCollatzX_processes(input, shR);
    }
    else {
        result = calcCollatz(input);
    }
    std::pair<uint64_t, uint32_t> to_send = std::make_pair(result, idx);
    assert(sizeof(to_send) <= PIPE_BUF);

    if (write(write_dsc, &to_send, sizeof(to_send)) != sizeof(to_send)) {
        std::cout << "Error in write\n";
        exit(-1);
    }
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

    // Shared memory
    SharedResults *shR = NULL;
    if (getSharedResults()) {
        int fd_memory = -1;
        int flags, prot;
        pid_t pid;

        prot = PROT_READ | PROT_WRITE;
        flags = MAP_SHARED | MAP_ANONYMOUS;
        shR = (SharedResults *) mmap(NULL, sizeof(SharedResults), prot, flags,
                                     fd_memory, 0);
        shR = getSharedResults().get();
    }

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
                new_process_TNP(pipe_dsc[1], contestInput[to_create - 1], to_create - 1, shR);
                exit(0);

            default:
                to_create--;
                if (contestInput.size() - to_create >= getSize()) {
                    readAndWriteResTNP(pipe_dsc[0], r);
                }
        }
    }
    for (uint32_t i = 0; i < std::min((uint32_t) contestInput.size(), getSize() - 1); i++) {
        readAndWriteResTNP(pipe_dsc[0], r);
    }
    for (uint32_t i = 0; i < contestInput.size(); i++)
        wait(0);

    return r;
}

static long MAX_WRITE = PIPE_BUF / sizeof(std::pair<uint64_t, uint32_t>);

void new_process_TCP(int read_dsc, int write_dsc, int id, int work, SharedResults *shR) {
    std::tuple<uint64_t, uint32_t, int> result; // result, index, process id

    for (int i = 0; i < work; i++) {
        std::string singleInput_str;
        std::string idx_str;
        char c;
        bool reading_idx = true;
        while (true) {
            if (read(read_dsc, &c, 1) != 1) {
                std::cout << "Error in read (new_process), errno = " << errno << "\n";
                exit(-1);
            }
            if (c == 0) {
                assert(!reading_idx);
                break;
            }
            else if (c == '-') {
                assert(reading_idx);
                reading_idx = false;
            }
            else if (reading_idx) {
                idx_str.push_back(c);
            }
            else {
                singleInput_str.push_back(c);
            }
        }
        int idx = std::stoi(idx_str);
        InfInt singleInput(singleInput_str);
        uint64_t res;
        if (shR) {
            res = calcCollatzX_processes(singleInput, shR);
        }
        else {
            res = calcCollatz(singleInput);
        }
        result = std::make_tuple(res, idx, id);
        assert(sizeof(result) <= PIPE_BUF);
        if (write(write_dsc, &result, sizeof(result)) != sizeof(result)) {
            std::cout << "Error in write\n";
            exit(-1);
        }
    }
}

ContestResult TeamConstProcesses::runContest(ContestInput const & contestInput)
{
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
        std::string id = std::to_string(i);
        char const *id_str = id.c_str();
        int work = work_per_process + (i < additional_work ? 1 : 0);
        std::string work_tmp_str = std::to_string(work);
        char const *work_str = work_tmp_str.c_str();

        for (int j = 0; j < std::min(work_per_process + (i < additional_work ? 1 : 0), (uint32_t) MAX_WRITE); j++) {
            std::string singleInput_str = contestInput[idx].toString();
            std::string idx_str = std::to_string(idx);
            std::string input_str = idx_str + '-' + singleInput_str;
            if (write(pipe_send_dsc[i][1], input_str.c_str(), input_str.size() + 1) != input_str.size() + 1) {
                std::cout << "Error in write\n";
                exit(-1);
            }
            idx++;
        }

        // Shared memory
        SharedResults *shR = NULL;
        if (getSharedResults()) {
            int fd_memory = -1;
            int flags, prot;
            pid_t pid;

            prot = PROT_READ | PROT_WRITE;
            flags = MAP_SHARED | MAP_ANONYMOUS;
            shR = (SharedResults *) mmap(NULL, sizeof(SharedResults), prot, flags,
                                                   fd_memory, 0);
            shR = getSharedResults().get();
        }

        switch (fork()) {
            case -1:
                std::cout << "Error in fork\n";
                exit(-1);

            case 0:
                close(pipe_send_dsc[i][1]);
                close(pipe_receive_dsc[0]);
                new_process_TCP(pipe_send_dsc[i][0], pipe_receive_dsc[1], i, work, shR);
                exit(0);
            default:
                close(pipe_send_dsc[i][0]);
        }
    }
    std::vector<long> work_left(getSize());
    for (uint32_t i = 0; i < getSize(); i++) {
        work_left[i] = std::max((long) 0, (long) work_per_process + (i < additional_work ? 1 : 0) - MAX_WRITE);
    }

    for (uint32_t i = 0; i < contestInput.size(); i++) {
        std::tuple<uint64_t, uint32_t, int> single_result;
        if (read(pipe_receive_dsc[0], &single_result, sizeof(single_result)) != sizeof(single_result)) {
            std::cout << "Error in read (main)\n";
            exit(-1);
        }
        uint64_t res = std::get<0>(single_result);
        uint32_t res_idx = std::get<1>(single_result);
        int process_id = std::get<2>(single_result);
        if (work_left[process_id] > 0) {
            std::string singleInput_str = contestInput[idx].toString();
            std::string idx_str = std::to_string(idx);
            std::string input_str = idx_str + '-' + singleInput_str;
            if (write(pipe_send_dsc[process_id][1], input_str.c_str(), input_str.size() + 1) != input_str.size() + 1) {
                std::cout << "Error in write\n";
                exit(-1);
            }
            work_left[process_id]--;
            idx++;
        }
        r[res_idx] = res;
    }
    for (uint32_t i = 0; i < getSize(); i++)
        wait(0);
    return r;
}

ContestResult TeamAsync::runContest(ContestInput const & contestInput)
{
    ContestResult r;
    std::vector<std::future<uint64_t>> results;
    std::shared_ptr<SharedResults> sharedResults = getSharedResults();
    for (auto singleInput : contestInput) {
        if (sharedResults) {
            results.push_back(std::async(calcCollatzX, singleInput, std::ref(sharedResults)));
        }
        else {
            results.push_back(std::async(calcCollatz, singleInput));
        }
    }
    for (auto &singleResult : results) {
        r.push_back(singleResult.get());
    }
    return r;
}
