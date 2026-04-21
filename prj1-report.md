# Cosmos+ OpenSSD Project#1

## OpenSSD 개발 환경 구축 및 NVMe 기반 Host Interface 실습

| 항목 | 내용 |
|------|------|
| 과목 | 임베디드시스템소프트웨어 |
| 담당 교수 | 김영재 |
| 이름 / 학번 | 홍길동 / 20241234 |
| 수행 기간 | 26.00.00 ~ 26.00.00 |

---

## 1. 프로젝트 개요 및 목표

본 프로젝트는 Cosmos+ OpenSSD 플랫폼 위에서 NVMe(Non-Volatile Memory Express) 기반의 Host Interface를 실습하는 것을 목표로 한다. 구체적으로 다음 세 가지 Task를 순차적으로 수행한다.

1. **Task 1 – 개발 환경 구축**: Cosmos+ OpenSSD 보드의 HW/SW 아키텍처를 이해하고, Xilinx SDK를 이용한 펌웨어 빌드 및 FPGA 비트스트림 로딩 환경을 구성한다.
2. **Task 2 – NVMe Passthru 기반 이미지 데이터 I/O**: 호스트 Linux 시스템에서 NVMe Passthru 인터페이스를 통해 이미지 파일을 SSD에 Write한 뒤 Read하여, 원본과 동일한지 바이트 단위로 검증한다.
3. **Task 3 – 새로운 NVMe 명령어 정의**: 호스트에서 커스텀 NVMe 명령어를 전송하면 SSD 펌웨어가 UART를 통해 자기소개를 출력하도록 NVMe I/O 명령어를 새로 정의한다.

전체 흐름은 "개발 환경 구축 → 표준 NVMe I/O 명령어 실습 → 커스텀 NVMe 명령어 확장"으로, SSD 내부 펌웨어와 호스트 인터페이스의 동작 원리를 단계적으로 체득하는 구조이다.

---

## 2. 프로젝트 내용) Task 1 – Cosmos+ OpenSSD 개발 환경 구축

### 2.1 Cosmos+ OpenSSD 플랫폼의 HW/SW 아키텍처

#### A. 하드웨어 아키텍처

Cosmos+ OpenSSD는 **Xilinx Zynq-7000 SoC**를 기반으로 한다. Zynq-7000은 다음 두 부분으로 구성된다.

| 구성 요소 | 역할 |
|-----------|------|
| **PS (Processing System)** | ARM Cortex-A9 듀얼코어 프로세서. FTL 펌웨어, NVMe 명령어 처리 등 소프트웨어 로직 수행 |
| **PL (Programmable Logic)** | FPGA 영역. NVMe 컨트롤러 IP, NAND Flash 컨트롤러(NSC), PCIe 인터페이스, DMA 엔진 등 하드웨어 가속 로직 구현 |

호스트 PC와는 **PCIe 인터페이스**를 통해 연결되며, NVMe 프로토콜로 통신한다. NAND Flash 칩들은 PL 내의 NSC(NAND Storage Controller)를 통해 다중 채널/웨이로 병렬 접근된다.

메모리 맵 구성 (`main.c` 기준):
- `0x00000000 ~ 0x001FFFFF` (2 MB): Cached & Buffered – 코드/데이터/힙/스택 영역
- `0x00200000 ~ 0x17FFFFFF` (~374 MB): Uncached & Nonbuffered – DMA 버퍼, NVMe 관리 영역
- `0x18000000 ~ 0x3FFFFFFF` (~640 MB): Cached & Buffered – 데이터 버퍼 영역

#### B. 소프트웨어 아키텍처

펌웨어는 크게 다음 계층으로 구성된다. 최상위의 호스트(Linux NVMe Driver)로부터 PCIe를 통해 명령이 전달되면, NVMe Controller IP(PL)가 이를 수신하고, PS 측의 NVMe Command Handler(`nvme_main.c`)가 Admin 명령어(`nvme_admin_cmd.c`)와 I/O 명령어(`nvme_io_cmd.c`)로 분류하여 처리한다. I/O 명령어는 Request Transform/Scheduling 계층(`request_transform.c`, `request_schedule.c`)을 거쳐 FTL(Flash Translation Layer)로 전달된다. FTL은 Address Translation, Data Buffer Management, Garbage Collection을 담당하며, 최종적으로 NSC Driver(`nsc_driver.c`)를 통해 NAND Flash 칩에 물리적으로 접근한다.

각 계층의 역할은 다음과 같다:

- **NVMe Main Loop** (`nvme_main.c`): 상태 머신 기반으로 NVMe 컨트롤러 상태(IDLE → WAIT_CC_EN → RUNNING → SHUTDOWN → RESET)를 관리하며, 커맨드 큐에서 명령어를 가져와 Admin/IO 핸들러로 디스패치한다.
- **FTL (GreedyFTL)**: 논리 주소(LBA)와 물리 주소(PBA) 간 매핑을 관리하며, Greedy 알고리즘 기반 Garbage Collection을 수행한다.
- **BSP (Board Support Package)**: Xilinx에서 제공하는 하드웨어 추상화 계층으로, GPIO, UART, I2C, QSPI, DMA, 타이머 등의 주변 장치 드라이버를 포함한다.

#### C. 부팅 및 초기화 과정

`main.c`의 초기화 순서는 다음과 같다:

1. I-Cache, D-Cache, MMU 비활성화
2. 페이징 테이블(TLB) 설정 – 메모리 영역별 캐시/버퍼 속성 지정
3. MMU 및 캐시 재활성화
4. 예외 핸들러 및 GIC(Generic Interrupt Controller) 초기화
5. PCIe 인터럽트(IRQ #61) 연결 및 활성화
6. `nvme_main()` 호출 → FTL 초기화 후 NVMe 메인 루프 진입

### 2.2 workspace/setup.sh 스크립트의 역할과 기능

`setup.sh` 스크립트는 Cosmos+ OpenSSD 개발 환경 구성을 자동화하는 역할을 한다. 주요 기능은 다음과 같다:

- FPGA 비트스트림(`.bit` 파일) 로딩
- 펌웨어 ELF 바이너리를 보드에 업로드 및 실행
- UART 시리얼 모니터(`tio`) 연결을 통한 디버그 메시지 확인
- PCIe 장치 재열거(re-enumerate) 지원

사용 예시:
```bash
./setup.sh GreedyFTL show    # 펌웨어 로드 후 tio로 시리얼 출력 확인
```

---

## 3. 프로젝트 내용) Task 2 – NVMe Passthru 기반 이미지 데이터 I/O

### 3.1 NVMe Passthru 인터페이스의 정의와 장단점

**NVMe Passthru**는 Linux 커널의 NVMe 드라이버가 제공하는 `ioctl` 기반 인터페이스로, 사용자 공간(User Space) 애플리케이션이 NVMe 명령어를 **직접 구성하여** SSD 컨트롤러에 전송할 수 있게 한다.

일반적인 블록 I/O 경로(`read()`/`write()` 시스템 콜)는 커널의 파일 시스템, 블록 계층, I/O 스케줄러를 거치지만, Passthru는 이 계층들을 **우회(bypass)**하여 NVMe Submission Queue에 명령어를 직접 넣는다.

**사용되는 ioctl 명령어**: `NVME_IOCTL_IO_CMD` (linux/nvme_ioctl.h에 정의)

| 장점 | 단점 |
|------|------|
| 표준 NVMe 명령어 외에 **벤더 고유(Vendor-specific) 명령어** 전송 가능 | 커널의 I/O 스케줄링, 캐싱 등의 최적화를 받지 못함 |
| NVMe 명령어의 모든 필드를 사용자가 직접 제어 가능 | 잘못된 명령어 전송 시 장치 오동작 위험 |
| 파일 시스템 없이도 Raw 데이터 I/O 가능 | 데이터 버퍼의 페이지 정렬(Page-aligned) 등 저수준 관리 필요 |
| SSD 내부 기능 테스트/디버깅에 적합 | 일반 사용자 애플리케이션 개발에는 부적합 |

### 3.2 NVMe Write/Read 명령어 구조와 각 필드의 역할

NVMe I/O 명령어는 64바이트 크기의 Submission Queue Entry(SQE)로 구성되며, 16개의 DWORD(각 4바이트)로 이루어진다. 각 DWORD의 역할은 다음과 같다.

- **DWORD 0** – Opcode(OPC), FUSE, PSDT, CID 필드를 포함한다. OPC는 명령어 종류를 나타내며 Write는 `0x01`, Read는 `0x02`이다. CID는 명령어의 고유 식별자이다.
- **DWORD 1** – NSID(Namespace Identifier)로, 대상 네임스페이스를 지정한다. 본 프로젝트에서는 NSID=1을 사용한다.
- **DWORD 2~5** – Reserved 영역 및 MPTR(Metadata Pointer)이다.
- **DWORD 6~7** – PRP1(Physical Region Page Entry 1)으로, 데이터 전송용 호스트 메모리의 물리 주소를 지정한다.
- **DWORD 8~9** – PRP2(Physical Region Page Entry 2)로, PRP List 또는 두 번째 페이지의 물리 주소를 지정한다.
- **DWORD 10~11** – Starting LBA(논리 블록 주소)로, 읽기/쓰기 시작 위치를 나타낸다.
- **DWORD 12** – NLB(Number of Logical Blocks)와 FUA(Force Unit Access) 등의 플래그를 포함한다. NLB는 0-based 값으로, 0이면 1블록을 의미한다.
- **DWORD 13~15** – DSM(Dataset Management), Protection Info 등 부가 정보를 포함한다.

이 구조는 펌웨어 측 `NVME_IO_COMMAND` 구조체(`nvme.h`)에 그대로 반영되어 있다:

```c
typedef struct _NVME_IO_COMMAND
{
    union {
        unsigned int dword[16];
        struct {
            struct {
                unsigned char OPC;
                unsigned char FUSE      :2;
                unsigned char reserved0 :5;
                unsigned char PSDT      :1;
                unsigned short CID;
            };
            unsigned int NSID;
            unsigned int reserved1[2];
            unsigned int MPTR[2];
            unsigned int PRP1[2];
            unsigned int PRP2[2];
            unsigned int dword10;
            unsigned int dword11;
            unsigned int dword12;
            unsigned int dword13;
            unsigned int dword14;
            unsigned int dword15;
        };
    };
} NVME_IO_COMMAND;
```

본 프로젝트에서 호스트 측 `nvme_passthru()` 함수가 실제로 사용하는 필드 매핑은 다음과 같다:

| `struct nvme_passthru_cmd` 필드 | 값 | 설명 |
|---|---|---|
| `opcode` | 0x01(Write) / 0x02(Read) | NVMe I/O 명령어 종류 |
| `nsid` | 1 | Namespace ID |
| `addr` | 호스트 버퍼 주소 | 데이터 전송용 메모리 주소 |
| `data_len` | 4096 (PAGE_SIZE) | 전송 데이터 크기 (바이트) |
| `cdw10` | LBA 번호 | 시작 논리 블록 주소 |
| `cdw12` | (chunk / BLOCK_SIZE) - 1 | NLB (0-based 블록 수) |
| `timeout_ms` | 5000 | 타임아웃 (밀리초) |

### 3.3 NVMe Write 시 SSD로의 데이터 (페이로드) 전송을 위한 메모리 할당 과정

이미지 데이터를 SSD에 쓰기 위해서는 NVMe Passthru가 요구하는 **페이지 정렬(Page-aligned) 메모리**를 할당해야 한다. 전체 과정은 다음과 같다:

1. **전체 전송 크기 계산**: 이미지 크기를 BLOCK_SIZE(4096바이트) 단위로 올림 정렬한다.
2. **정렬 버퍼 할당**: `posix_memalign()`으로 PAGE_SIZE(4096) 경계에 정렬된 메모리를 할당한다. NVMe DMA 전송 시 커널이 사용자 버퍼를 물리 메모리에 매핑하는데, 이때 페이지 경계 정렬이 필수적이다.
3. **청크 단위 전송**: PAGE_SIZE(4096바이트) 단위로 이미지 데이터를 정렬 버퍼에 복사한 뒤, NVMe Write 명령어를 구성하여 `ioctl()`로 전송한다. 마지막 청크가 BLOCK_SIZE보다 작을 경우 남은 영역은 0으로 패딩된다.
4. **LBA 증가**: 각 청크 전송 후 LBA를 `chunk / BLOCK_SIZE`만큼 증가시켜 다음 위치에 기록한다.

**ImageWrite 함수 코드** (`nvme_passthru.cc`):

```cpp
int Embedded::Proj1::ImageWrite(const std::vector<uint8_t> &buf)
{
  if (buf.empty())
    return -EINVAL;
  if (buf.size() > MAX_BUFLEN)
  {
    cerr << "[ERROR] Image size exceeds the maximum transfer size limit." << endl;
    return -EINVAL;
  }

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
    uint32_t copy_len = (offset + chunk <= buf.size())
        ? chunk
        : (buf.size() > offset ? buf.size() - offset : 0);
    if (copy_len > 0)
      memcpy(chunk_buf, buf.data() + offset, copy_len);

    my_cmd my;
    my.opcode = NVME_CMD_WRITE;
    my.nsid   = NSID;
    my.addr   = (uint64_t)chunk_buf;
    my.size   = chunk;
    my.cdw12  = (chunk / BLOCK_SIZE) - 1;
    my.cdw10  = lba;

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
```

`nvme_passthru()` 내부에서는 `struct nvme_passthru_cmd`를 구성하고, `ioctl(fd_, NVME_IOCTL_IO_CMD, &cmd)`를 호출하여 커널 NVMe 드라이버에 명령을 전달한다.

---

## 4. 프로젝트 내용) Task 3 – 새로운 NVMe 명령어 (자기소개용) 정의

### 4.1 OpenSSD 펌웨어의 NVMe 명령어 처리 간 함수 호출 흐름 및 함수별 역할

호스트에서 NVMe I/O 명령어가 전송되면, 펌웨어에서 다음과 같은 순서로 처리된다.

**1단계: 호스트 → 커널 → PCIe** – 호스트 애플리케이션이 `ioctl(NVME_IOCTL_IO_CMD)`를 호출하면, Linux NVMe 드라이버가 해당 명령어를 SQE(Submission Queue Entry) 형태로 Submission Queue에 삽입하고 Doorbell 레지스터를 갱신한다.

**2단계: NVMe Controller IP (FPGA)** – PL(Programmable Logic) 영역의 NVMe 컨트롤러 IP가 PCIe를 통해 SQE를 수신하여 CMD SRAM에 저장하고, PS(Processing System) 측에 인터럽트(IRQ #61)를 발생시킨다.

**3단계: `dev_irq_handler()` (host_lld.c)** – ARM Cortex-A9의 인터럽트 핸들러가 호출되어 IRQ 상태를 확인하고, NVMe 태스크 상태 전이를 트리거한다.

**4단계: `nvme_main()` (nvme_main.c)** – 메인 루프에서 `get_nvme_cmd()`를 호출하여 CMD FIFO에서 명령어를 디큐한다. `qID == 0`이면 Admin 명령어로 `handle_nvme_admin_cmd()`를 호출하고, `qID != 0`이면 I/O 명령어로 `handle_nvme_io_cmd()`를 호출한다.

**5단계: `handle_nvme_io_cmd()` (nvme_io_cmd.c)** – I/O 명령어의 OPC(Opcode)에 따라 switch-case로 분기한다. `0x00`(Flush)이면 `set_auto_nvme_cpl()`로 즉시 완료 응답을 보내고, `0x01`(Write)이면 `handle_nvme_io_write()`를, `0x02`(Read)이면 `handle_nvme_io_read()`를 호출한다. 새로 추가한 `0x99`(Hello)이면 `xil_printf()`로 UART에 자기소개를 출력한다.

**6단계: Write/Read 경로** – `handle_nvme_io_write()` 및 `handle_nvme_io_read()`는 명령어에서 LBA와 NLB를 추출한 뒤 `ReqTransNvmeToSlice()`를 호출하여 NVMe 요청을 FTL 내부 슬라이스 단위 요청으로 변환한다. 이후 `nvme_main()`으로 돌아와 `ReqTransSliceToLowLevel()`이 호출되어 슬라이스 요청을 NAND Flash 물리 접근 요청으로 변환한다.

각 함수의 역할을 정리하면 다음과 같다:

| 함수 | 파일 | 역할 |
|------|------|------|
| `dev_irq_handler()` | `host_lld.c` | PCIe 인터럽트 수신 및 NVMe 태스크 상태 전이 트리거 |
| `nvme_main()` | `nvme_main.c` | NVMe 컨트롤러 상태 머신 운영, 커맨드 큐에서 명령어 디큐 및 디스패치 |
| `get_nvme_cmd()` | `host_lld.c` | CMD FIFO 레지스터에서 유효한 NVMe 명령어를 읽어옴 |
| `handle_nvme_io_cmd()` | `nvme_io_cmd.c` | I/O 명령어의 Opcode에 따라 적절한 핸들러로 분기 (switch-case) |
| `handle_nvme_io_write()` | `nvme_io_cmd.c` | Write 명령어 처리: LBA, NLB 추출 후 `ReqTransNvmeToSlice()` 호출 |
| `handle_nvme_io_read()` | `nvme_io_cmd.c` | Read 명령어 처리: LBA, NLB 추출 후 `ReqTransNvmeToSlice()` 호출 |
| `ReqTransNvmeToSlice()` | `request_transform.c` | NVMe 요청을 FTL 내부 슬라이스 단위 요청으로 변환 |
| `ReqTransSliceToLowLevel()` | `request_transform.c` | 슬라이스 요청을 NAND 물리 접근 요청으로 변환 |
| `set_auto_nvme_cpl()` | `host_lld.c` | NVMe Completion Queue에 완료 응답 기록 |

### 4.2 새로운 NVMe 명령어 정의를 위한 최소 수정 사항

커스텀 NVMe 명령어 `HELLO` (Opcode: `0x99`)를 정의하기 위해 수정한 파일과 내용은 다음과 같다.

#### 수정 파일 1: `nvme.h` (펌웨어 측 – Opcode 정의)

기존 I/O 명령어 Opcode 목록에 `IO_NVM_HELLO`를 추가하였다.

```c
/* Opcodes for IO Commands */
#define IO_NVM_FLUSH              0x00
#define IO_NVM_WRITE              0x01
#define IO_NVM_READ               0x02
#define IO_NVM_WRITE_UNCORRECTABLE 0x04
#define IO_NVM_COMPARE            0x05
#define IO_NVM_DATASET_MANAGEMENT 0x09
#define IO_NVM_HELLO              0x99   // 추가: 자기소개 명령어
```

#### 수정 파일 2: `nvme_io_cmd.c` (펌웨어 측 – 명령어 핸들러)

`handle_nvme_io_cmd()` 함수의 `switch` 문에 `IO_NVM_HELLO` 케이스를 추가하였다. 해당 명령어가 수신되면 `xil_printf()`를 통해 UART로 자기소개 메시지를 출력한다.

```c
void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd)
{
    NVME_IO_COMMAND *nvmeIOCmd;
    NVME_COMPLETION nvmeCPL;
    unsigned int opc;
    nvmeIOCmd = (NVME_IO_COMMAND*)nvmeCmd->cmdDword;
    opc = (unsigned int)nvmeIOCmd->OPC;

    switch(opc)
    {
        case IO_NVM_HELLO:
        {
            xil_printf("hi my name is tomato");
            break;
        }
        case IO_NVM_FLUSH:
        {
            nvmeCPL.dword[0] = 0;
            nvmeCPL.specific = 0x0;
            set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
            break;
        }
        case IO_NVM_WRITE:
        {
            handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd);
            break;
        }
        case IO_NVM_READ:
        {
            handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd);
            break;
        }
        default:
        {
            xil_printf("Not Support IO Command OPC: %X\r\n", opc);
            ASSERT(0);
            break;
        }
    }
}
```

#### 수정 파일 3: `nvme_passthru.cc` (호스트 측 – Hello 함수 및 Passthru 구현)

`Hello()` 함수는 Opcode `0x99`를 가진 NVMe 명령어를 구성하여 `nvme_passthru()`를 통해 전송한다. `nvme_passthru()` 내부에서는 `0x99` opcode를 별도로 분기 처리하여, 데이터 전송 없이 명령어만 전송하도록 구현하였다.

```cpp
int Embedded::Proj1::Hello()
{
    my_cmd my;
    my.opcode = 0x99;
    int ret = nvme_passthru(&my);
    if (ret < 0)
        return ret;
    return 0;
}

int Embedded::Proj1::nvme_passthru(my_cmd *my)
{
    struct nvme_passthru_cmd cmd = {};

    if (my->opcode == 0x99) {
        cmd.opcode = my->opcode;
        int ret = ioctl(fd_, NVME_IOCTL_IO_CMD, &cmd);
        if (ret != 0) {
            perror("[HELLO] ioctl NVME_IOCTL_IO_CMD failed");
            return ret < 0 ? ret : -EIO;
        }
        return 0;
    }

    cmd.opcode     = my->opcode;
    cmd.nsid       = my->nsid;
    cmd.addr       = my->addr;
    cmd.data_len   = my->size;
    cmd.cdw10      = my->cdw10;
    cmd.cdw11      = 0;
    cmd.cdw12      = my->cdw12;
    cmd.timeout_ms = 5000;

    int ret = ioctl(fd_, NVME_IOCTL_IO_CMD, &cmd);
    if (ret != 0) {
        perror("[RW] ioctl NVME_IOCTL_IO_CMD failed");
        return ret < 0 ? ret : -EIO;
    }
    return 0;
}
```

#### 수정 파일 4: `test.cc` (호스트 측 – Task 3 활성화)

`TASK3_ON` 매크로를 정의하여 Task 3 모드를 활성화한다.

```cpp
#define TASK3_ON

int main(int argc, char *argv[]) {
    Proj1 dev;
    if (dev.Open(DEV_PATH) < 0) {
        cerr << "Device open failed" << endl;
        return -1;
    }

#ifdef TASK3_ON
    cout << "Sending custom HELLO command to device..." << endl;
    if (dev.Hello() < 0) {
        cerr << "HELLO command failed" << endl;
        return -1;
    }
    cout << "HELLO command sent successfully. Check 'tio' output for device message." << endl;
    return 0;
#else
    // Task 2: Image Write/Read ...
#endif
}
```

#### 수정 요약

| 수정 파일 | 위치 | 수정 내용 |
|-----------|------|-----------|
| `nvme.h` | 펌웨어 | `#define IO_NVM_HELLO 0x99` 추가 |
| `nvme_io_cmd.c` | 펌웨어 | `switch`문에 `case IO_NVM_HELLO` 추가, `xil_printf()` 호출 |
| `nvme_passthru.cc` | 호스트 | `Hello()` 함수 구현 (opcode 0x99 전송) |
| `nvme_passthru.cc` | 호스트 | `nvme_passthru()`에서 0x99 opcode 분기 처리 |
| `test.cc` | 호스트 | `#define TASK3_ON` 활성화 |

---

## 5. 참고 문헌

1. NVM Express Base Specification, Revision 1.4, NVM Express, Inc., 2019.
   - NVMe 프로토콜의 커맨드 구조, Submission/Completion Queue, I/O 명령어(Write, Read, Flush) 등의 공식 사양 참조

2. J. Kwak, S. Lee, K. Park, J. Jeong, and Y. H. Song, "Cosmos+ OpenSSD: Rapid Prototype for Flash Storage Systems," *ACM Transactions on Storage*, vol. 16, no. 3, Article 15, Aug. 2020.
   - Cosmos+ OpenSSD 플랫폼의 아키텍처, GreedyFTL 설계, 호스트 인터페이스 구현에 대한 학술 논문

3. Linux Kernel Source: `include/uapi/linux/nvme_ioctl.h`, v5.15.
   - `struct nvme_passthru_cmd` 및 `NVME_IOCTL_IO_CMD` 정의 참조
   - https://elixir.bootlin.com/linux/v5.15/source/include/uapi/linux/nvme_ioctl.h
