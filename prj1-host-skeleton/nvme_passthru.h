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
        unsigned char opcode;
        unsigned int nsid;
        unsigned long addr;
        unsigned int size;
        unsigned int cdw12;
        // unsigned_int prp1[2];
        // unsigned_int prp2[2];
    };
    class Proj1 {
        public:
            Proj1() : fd_(-1) {}
            int Open(const std::string &dev);
            int ImageWrite(const std::vector<uint8_t> &buf);
            int ImageRead(std::vector<uint8_t> &buf, size_t size);
            int Hello();
        private:
            int fd_;
            int nvme_passthru(my_cmd * my);
    };
}
