#include <iostream>
#include <unistd.h>
#include "lib/infint/InfInt.h"

#include "collatz.hpp"

int main(int argc, char *argv[])
{
    if (argc == 4) { // TeamNewProcesses
        //std::cout << "Got in the new_process program.\n";
        uint64_t result;

        // Arguments:
        int write_dsc = atoi(argv[1]);
        InfInt input(argv[2]);
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
    else if (argc == 5) { // TeamConstProcesses
        //std::pair<uint64_t, uint32_t> input;
        std::tuple<uint64_t, uint32_t, int> result; // result, index, process id
        int read_dsc = atoi(argv[1]), write_dsc = atoi(argv[2]);
        int id = atoi(argv[3]);
        int work = atoi(argv[4]);

        for (int i = 0; i < work; i++) {
            //std::cout << "Read dsc = " << read_dsc << ", sizeof(input) = " << sizeof(input) << "\n";
            std::string singleInput_str;
            std::string idx_str;
            char c;
            //std::cout << "Reading single input ... (new_process " << id << ")\n";
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
            //std::cout << "idx_str = " << idx_str << ", singleInput_str = " << singleInput_str << "\n";
            //std::cout << "idx = " << idx << ", singleInput = " << singleInput << "\n";
            /*if (read(read_dsc, &input_str, sizeof(input)) != sizeof(input)) {
                std::cout << "Error in read (new_process), errno = " << errno << "\n";
                exit(-1);
            }*/
            result = std::make_tuple(calcCollatz(singleInput), idx, id);
            assert(sizeof(result) <= PIPE_BUF);
            //std::cout << "Writing single result ... (new_process " << id << ")\n";
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