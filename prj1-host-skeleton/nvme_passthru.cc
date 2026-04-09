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
  err = open(dev.c_str(), O_RDWR);
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
  uint32_t total_aligned = ((buf.size() + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;

  void *chunk_buf = NULL;
  if (posix_memalign(&chunk_buf, PAGE_SIZE, PAGE_SIZE) != 0)
    return -ENOMEM;

  uint32_t offset = 0;
  uint32_t lba = 0;
  while (offset < total_aligned) {
    uint32_t chunk = PAGE_SIZE;
    if (offset + chunk > total_aligned)
      chunk = total_aligned - offset;

    memset(chunk_buf, 0, PAGE_SIZE);
    uint32_t copy_len = (offset + chunk <= buf.size()) ? chunk : (buf.size() > offset ? buf.size() - offset : 0);
    if (copy_len > 0)
      memcpy(chunk_buf, buf.data() + offset, copy_len);

    my_cmd my;
    my.opcode = NVME_CMD_WRITE;
    my.nsid = NSID;
    my.addr = (uint64_t)chunk_buf;
    my.size = chunk;
    my.cdw12 = (chunk / BLOCK_SIZE) - 1;
    my.cdw10 = lba;

    int ret = nvme_passthru(&my);
    if (ret < 0) {
      free(chunk_buf);
      return ret;
    }

    offset += chunk;
    lba += chunk / BLOCK_SIZE;
  }

  free(chunk_buf);
  return 0;
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

  uint32_t total_aligned = ((size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;

  void *chunk_buf = NULL;
  if (posix_memalign(&chunk_buf, PAGE_SIZE, PAGE_SIZE) != 0)
    return -ENOMEM;

  buf.resize(size);
  uint32_t offset = 0;
  uint32_t lba = 0;
  while (offset < total_aligned) {
    uint32_t chunk = PAGE_SIZE;
    if (offset + chunk > total_aligned)
      chunk = total_aligned - offset;

    memset(chunk_buf, 0, PAGE_SIZE);

    my_cmd my;
    my.opcode = NVME_CMD_READ;
    my.nsid = NSID;
    my.addr = (uint64_t)chunk_buf;
    my.size = chunk;
    my.cdw12 = (chunk / BLOCK_SIZE) - 1;
    my.cdw10 = lba;

    int ret = nvme_passthru(&my);
    if (ret < 0) {
      free(chunk_buf);
      return ret;
    }

    uint32_t copy_len = (offset + chunk <= size) ? chunk : (size > offset ? size - offset : 0);
    if (copy_len > 0)
      memcpy(buf.data() + offset, chunk_buf, copy_len);

    offset += chunk;
    lba += chunk / BLOCK_SIZE;
  }

  free(chunk_buf);
  return 0;
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

  struct nvme_passthru_cmd cmd = {};

  cmd.opcode = my->opcode;
  cmd.nsid = my->nsid;
  cmd.addr = my->addr;
  cmd.data_len = my->size;
  cmd.cdw10 = my->cdw10;
  cmd.cdw11 = 0;
  cmd.cdw12 = my->cdw12;
  cmd.timeout_ms = 5000;

  fprintf(stderr, "[DEBUG] ioctl: opcode=0x%02x lba=%u nblocks=%u data_len=%u\n",
          cmd.opcode, cmd.cdw10, cmd.cdw12 + 1, cmd.data_len);

  int ret = ioctl(fd_, NVME_IOCTL_IO_CMD, &cmd);
  if (ret != 0) {
    perror("ioctl NVME_IOCTL_IO_CMD failed");
    fprintf(stderr, "  ret=%d\n", ret);
    return ret < 0 ? ret : -EIO;
  }

  return 0;
}