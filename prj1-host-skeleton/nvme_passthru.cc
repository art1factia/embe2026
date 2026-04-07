#include "nvme_passthru.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>
#include <fcntl.h>
#include <cstring>
#include <cstdint>
#include <stdlib.h>
#include <vector>
#include <iostream>
#include <cstdio>
#include <inttypes.h>

#define FLUSH 0x00
#define WRITE 0x01
#define READ 0x02
#define BLOCK_SIZE 512

using namespace std;

const unsigned int PAGE_SIZE = 4096;
const unsigned int MAX_BUFLEN = 16 * 1024 * 1024; /* Maximum transfer size (can be adjusted if needed) */
const unsigned int NSID = 1;                      /* NSID can be checked using 'sudo nvme list' */

int Embedded::Proj1::Open(const std::string &dev)
{
  int err;
  err = open(dev.c_str(), O_RDONLY);
  if (err < 0)
    return -1;
  fd_ = err;

  struct stat nvme_stat;
  err = fstat(fd_, &nvme_stat);
  if (err < 0)
    return -1;
  if (!S_ISCHR(nvme_stat.st_mode) && !S_ISBLK(nvme_stat.st_mode))
    return -1;

  return 0;
}

int Embedded::Proj1::ImageWrite(const std::vector<uint8_t> &buf)
{
  if (buf.empty())
    return -EINVAL;
  if (buf.size() > MAX_BUFLEN)
  {
    cerr << "[ERROR] Image size exceeds the maximum transfer size limit." << endl;
    return -EINVAL;
  }

  /* ------------------------------------------------------------------
   * TODO: Implement this function.
   *
   * Return 0 on success, or a negative error code on failure.
   * ------------------------------------------------------------------ */
  my_cmd *my;
  my->opcode = NVME_CMD_WRITE;
  my->nsid = 1; // ??

  uint32_t aligned = ((buf.size() + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
  buf.resize(aligned);
  my->addr = (uint64_t)buf.data();
  my->size = aligned;
  my->cdw12 = (aligned / BLOCK_SIZE) - 1;
  nvme_passthru(my);

  // __u32 dword_10 = static_cast<uint32_t> (addr & 0xFFFFFFFF);
  // __u32 dword_11 = static_cast<uint32_t> ((addr >> 32) & 0xFFFFFFFF);
  // my->dword[10] =dword_10;
  // my->dword[11] = dword_11;
  return -1; // placeholder
}

int Embedded::Proj1::ImageRead(std::vector<uint8_t> &buf, size_t size)
{
  if (size == 0)
    return -EINVAL;
  if (size > MAX_BUFLEN)
  {
    cerr << "[ERROR] Requested read size exceeds the maximum transfer size limit." << endl;
    return -EINVAL;
  }

  /* ------------------------------------------------------------------
   * TODO: Implement this function.
   *
   * Return 0 on success, or a negative error code on failure.
   * ------------------------------------------------------------------ */

  my_cmd *my;
  my->opcode = NVME_CMD_READ;
  my->nsid = 1; // ??

  uint32_t aligned = ((size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
  buf.resize(aligned);
  my->addr = (uint64_t)buf.data();
  my->size = aligned;
  my->cdw12 = (aligned / BLOCK_SIZE) - 1;
  nvme_passthru(my);

  return -1; // placeholder
}

int Embedded::Proj1::Hello()
{
  /* ------------------------------------------------------------------
   * TODO: Implement this function.
   *
   * Return 0 on success, or a negative error code on failure.
   * ------------------------------------------------------------------ */

  return -1; // placeholder
}

int Embedded::Proj1::nvme_passthru(my_cmd *my)
{
  /* ------------------------------------------------------------------
   * TODO: Implement this function.
   * This function should serve as the low-level interface for issuing
   * passthru NVMe commands. Make sure to include appropriate arguments
   * (e.g., opcode, namespace ID, command dwords, buffer pointer, length,
   * and result field) so that higher-level methods (ImageWrite, ImageRead,
   * Hello) can be implemented using this helper.
   *
   * Hint: refer to the Linux nvme_ioctl.h header and the struct nvme_passthru_cmd definition.
   * - Link: https://elixir.bootlin.com/linux/v5.15/source/include/uapi/linux/nvme_ioctl.h
   * ------------------------------------------------------------------ */

  // ioctl syscall(user custom command, nvme_io_cmd struct) -> driver -> firmware
  // /home/embe2024/workspace/GreedyFTL/cosmos_app/src/nvme/nvme_io_cmd.c
  struct nvme_passthru_cmd *cmd;

  switch (my->opcode)
  {
  case NVME_CMD_READ:
    cmd->opcode = NVME_CMD_READ;
    cmd->addr = my->addr;
    cmd->nsid = my->nsid;
    cmd->data_len = my->size;
    cmd->cdw10 = 0;
    cmd->cdw11 = 0;
    cmd->cdw12 = my->cdw12;
    break;
  case NVME_CMD_WRITE:
    cmd->opcode = NVME_CMD_WRITE;
    cmd->addr = my->addr;
    cmd->nsid = my->nsid;
    cmd->data_len = my->size;
    cmd->cdw10 = 0;
    cmd->cdw11 = 0;
    cmd->cdw12 = my->cdw12;
    break;
  }
  ioctl(fd_, NVME_IOCTL_IO_CMD, cmd);

  return -1; // placeholder
}