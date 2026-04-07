#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Embedded {
    enum nvme_opcode {
        NVME_CMD_WRITE                  = 0x01,
        NVME_CMD_READ                   = 0x02,
        /* Additional opcodes may be defined here */ 
    };
    struct my_cmd{
        __u8 opcode;
        __u32 nsid;
        __u64 addr;
        __u32 size;
        __u32 cdw12;
        // unsigned_int prp1[2];
        // unsigned_int prp2[2];
    }
    class Proj1 {
        public:
            Proj1() : fd_(-1) {}
            int Open(const std::string &dev);
            int ImageWrite(const std::vector<uint8_t> &buf);
            int ImageRead(std::vector<uint8_t> &buf, size_t size);
            int Hello();
        private:
            int fd_;
            int nvme_passthru(/* TODO: Define the function parameters here */);
    };
}
