#include <iostream>
#include <unistd.h>
#include "lib/infint/InfInt.h"

#include "collatz.hpp"

int main(int argc, char *argv[])
{
    if (argc == 4) {
        //std::cout << "Got in the new_process program.\n";
        uint64_t result;

        // Arguments:
        int write_dsc = atoi(argv[1]);
        InfInt input = atoll(argv[2]);
        uint32_t idx = atoi(argv[3]);

        result = calcCollatz(input);
        std::pair<uint64_t, uint32_t> to_send = std::make_pair(result, idx);
        assert(sizeof(to_send) <= PIPE_BUF);

        if (write(write_dsc, &to_send, sizeof(to_send)) != sizeof(to_send)) {
            std::cout << "Error in write\n";
            exit(-1);
        }
        //std::cout << "Ending the new_process program.\n";
    }
    else if (argc == 5) {
        std::pair<uint64_t, uint32_t> input;
        std::tuple<uint64_t, uint32_t, int> result; // result, index, process id
        int read_dsc = atoi(argv[1]), write_dsc = atoi(argv[2]);
        int id = atoi(argv[3]);
        int work = atoi(argv[4]);

        for (int i = 0; i < work; i++) {
            //std::cout << "Read dsc = " << read_dsc << ", sizeof(input) = " << sizeof(input) << "\n";
            //std::cout << "Reading input ... (new_process)\n";
            if (read(read_dsc, &input, sizeof(input)) != sizeof(input)) {
                std::cout << "Error in read (new_process), errno = " << errno << "\n";
                exit(-1);
            }
            result = std::make_tuple(calcCollatz(input.first), input.second, id);
            assert(sizeof(result) <= PIPE_BUF);
            //std::cout << "Writing single result ... (new_process)\n";
            if (write(write_dsc, &result, sizeof(result)) != sizeof(result)) {
                std::cout << "Error in write\n";
                exit(-1);
            }
        }
    }
    else {
        std::cout << "Error in passing arguments to child process\n";
        exit(-1);
    }
    exit(0);
}