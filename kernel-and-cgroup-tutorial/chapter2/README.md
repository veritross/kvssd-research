# 2장: Ramdisk 디바이스 드라이버 구현

Ramdisk는 디바이스 드라이버를 제작하는 가장 기초적인 과정으로 이번 챕터를 통해서 MQ 블록 레이어 기반 Ramdisk를 생성해보도록 하겠습니다.

## 설치해야 할 것

다음 명령어를 통해서 필요한 프로그램을 추가로 다운받도록 합니다.

```bash
sudo apt update -y
sudo apt install -y clang-format
```

## HelloWorld 드라이버의 생성

먼저 Makefile은 다음과 같이 작성을 해주시면 됩니다.

```make
obj-m += ramdisk.o
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

format:
	clang-format -i *.c

clean:
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean
```

`obj-m`에서도 알 수 있듯이, 파일 이름은 `ramdisk.c` 파일로 설정을 해주도록 합니다.

각각 명령은 다음을 실행합니다.

```bash
make all # 리눅스 커널 모듈을 생성을 합니다.
make format # 소스 코드에 대한 포맷팅을 실시합니다.
make clean # 리눅스 커널 모듈의 파생 파일을 삭제하는데 사용합니다.
```

그리고 다음과 같은 파일을 작성합니다.

```c
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int __init ramdisk_init(void) {
    int ret = 0;
    printk(KERN_INFO "hello world\n");
    return ret;
}

static void __exit ramdisk_exit(void)
{
    printk(KERN_INFO "bye bye!!\n");
}

module_init(ramdisk_init);
module_exit(ramdisk_exit);

MODULE_AUTHOR("BlaCkinkGJ");
MODULE_LICENSE("GPL");
```

이 다음에 해당 파일을 Makefile을 활용해서 빌드를 수행하고, 다음 명령어를 실행해주도록 합시다.

```bash
sudo insmod ramdisk.ko
```

> 만약에 `sudo dmesg`를 해서 문제가 발생했으면 다음 명령어를 실행해본다.
> 문제 내용: "module: x86/modules: Skipping invalid relocation target, existing value is nonzero for type 1"

```bash
sudo apt update && sudo apt upgrade
sudo apt remove --purge linux-headers-*
sudo apt autoremove && sudo apt autoclean
sudo apt install linux-headers-generic
sudo reboot
```

그리고 다음 명령어를 수행하고 dmesg 결과를 확인하도록 합니다.

```bash
sudo rmmod ramdisk.ko
sudo dmesg
```

그러면 "hello world"와 "bye bye!!"가 정상적으로 나타남을 확인할 수 있습니다.
여기서 알 수 있는 사실은 `printk()`를 통해서 출력한 내용은 dmesg에 작성됨을 알 수 있습니다.

## ramdisk_init() 구현

ramdisk의 이름과 크기 및 세그먼트(페이지 안에서 연속된 공간을 지칭)의 최대 개수와 크기를 설정하는 상수를 정의합니다.

```c
#define RAMDISK_DISK_NAME "ramdisk"
#define RAMDISK_CAPACITY_BYTES (40 << 20) // 40MiB
#define RAMDISK_MAX_SEGMENTS (32)
#define RAMDISK_MAX_SEGMENT_SIZE (64 << 10) // 64KiB
```

일단 ramdisk 정보가 들어갈 타입을 생성해주도록 합니다.

```c
struct ramdisk_dev_t {
    sector_t capacity;
    u8 *storage;

    struct blk_mq_tag_set tag_set;

    struct gendisk *disk;
};
```

그리고 블록 장치의 major(장치 종류)과 minor(장치 그 자체) 값 설정을 위한  전역 변수를 선언합니다.
참고로 ramdisk는 추가적인 장치를 받지 않으므로, `ramdisk_dev_t` 타입의 변수는 하나만 유지합니다.

> 만약에 여러 장비를 지원하고 싶을 때, 어떻게 해야할지 생각해보시면 좋을 것 같습니다.

```c
static int major;
static DEFINE_IDA(ramdisk_indices); // minor 값을 획득하는데 사용됩니다.
static struct ramdisk_dev_t *ramdisk_dev = NULL;
```

그리고 명령어 집합을 가지는 전역 변수와 명령을 수행하는 함수 초안을 작성합니다.
함수 내용은 뒤에 "ramdisk_queue_rq() 구현"에서 다루도록 하겠습니다.

```c
// 실제로 I/O가 처리되는 함수
static blk_status_t ramdisk_queue_rq(struct blk_mq_hw_ctx *hctx,
				     const struct blk_mq_queue_data *bd)
{
	// TODO: 뒤에서 작성할 예정
	return BLK_STS_OK;
}

// ramdisk를 위한 MQ Block Layer를 위한 명령어 집합
static const struct blk_mq_ops ramdisk_mq_ops = {
	.queue_rq = &ramdisk_queue_rq,
};

// ramdisk를 위한 device 명령을 위한 명령어 집합
static const struct block_device_operations ramdisk_rq_ops = {
	.owner = THIS_MODULE,
};
```

이제 `ramdisk_init()` 부분을 작성해봅시다.

```c
static int __init ramdisk_init(void)
{
	int ret = 0;
	int minor;
	struct gendisk *disk;

	// 블록 디바이스 리스트에 등록합니다.
	// ret 값에는 major 값이 들어갑니다.
	ret = register_blkdev(0, RAMDISK_DISK_NAME);
	if (ret < 0) {
		goto fail;
	}
	major = ret;
	pr_info("ramdisk registered\n");

	// ramdisk 구조체를 초기화합니다.
	ramdisk_dev = kzalloc(sizeof(struct ramdisk_dev_t), GFP_KERNEL);
	if (ramdisk_dev == NULL) {
		pr_err("memory allocation failed for ramdisk_dev\n");
		ret = -ENOMEM;
		goto fail;
	}
	pr_info("ramdisk structure is initialized\n");

	// ramdisk에서 메모리에 데이터를 저장하기 위한 메모리 공간을 확보합니다.
	ramdisk_dev->capacity = (RAMDISK_CAPACITY_BYTES >> SECTOR_SHIFT);
	ramdisk_dev->storage = kvmalloc(RAMDISK_CAPACITY_BYTES, GFP_KERNEL);
	if (ramdisk_dev->storage == NULL) {
		pr_err("memory allocation failed for ramdisk_dev\n");
		ret = -ENOMEM;
		goto fail_free_ramdisk_dev;
	}
	pr_info("memory space for ramdisk is successfully allocated\n");

	// MQ Block Layer에서 사용되는 Tag Set을 초기화합니다.
	memset(&ramdisk_dev->tag_set, 0, sizeof(ramdisk_dev->tag_set));
	ramdisk_dev->tag_set.ops = &ramdisk_mq_ops;
	ramdisk_dev->tag_set.queue_depth = 128;
	ramdisk_dev->tag_set.numa_node = NUMA_NO_NODE;
	ramdisk_dev->tag_set.flags =
		BLK_MQ_F_SHOULD_MERGE; // block layer merges contiguous requests
	ramdisk_dev->tag_set.cmd_size = 0;
	ramdisk_dev->tag_set.driver_data = ramdisk_dev;
	ramdisk_dev->tag_set.nr_hw_queues = 1; // the number of hardware queues.
	ret = blk_mq_alloc_tag_set(&ramdisk_dev->tag_set);
	if (ret < 0) {
		goto fail_free_ramdisk_dev_storage;
	}
	pr_info("tag set info for blk_mq is configured\n");

	// gendisk 값 설정을 위한 과정을 진행합니다.
	disk = blk_mq_alloc_disk(&ramdisk_dev->tag_set, ramdisk_dev);
	blk_queue_logical_block_size(disk->queue, PAGE_SIZE);
	blk_queue_physical_block_size(disk->queue, PAGE_SIZE);
	blk_queue_max_segments(disk->queue, RAMDISK_MAX_SEGMENTS);
	blk_queue_max_segment_size(disk->queue, RAMDISK_MAX_SEGMENT_SIZE);
	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		pr_err("allocating a disk failed\n");
		goto fail_free_ramdisk_dev_storage;
	}
	ramdisk_dev->disk = disk;
	pr_info("gendisk info is configured\n");

	// minor 값을 할당을 받아옵니다.
	ret = ida_alloc(&ramdisk_indices, GFP_KERNEL);
	if (ret < 0) {
		goto fail_free_disk;
	}
	minor = ret;
	pr_info("minor number is retrieved\n");

	// gendisk의 각 필드 값을 할당을 해줍니다.
	disk->major = major;
	disk->first_minor = minor;
	disk->minors = 1;
	snprintf(disk->disk_name, DISK_NAME_LEN, RAMDISK_DISK_NAME);
	disk->fops = &ramdisk_rq_ops;
	disk->flags =
		GENHD_FL_NO_PART; // 파티션 기능을 지원하지 않음을 표기합니다.
	set_capacity(disk, ramdisk_dev->capacity);

	ret = add_disk(disk);
	if (ret < 0) {
		goto fail_free_disk;
	}
	pr_info("ramdisk module is successfully loaded\n");

	return 0;
fail_free_disk:
	put_disk(ramdisk_dev->disk);
fail_free_ramdisk_dev_storage:
	kfree(ramdisk_dev->storage);
fail_free_ramdisk_dev:
	kfree(ramdisk_dev);
fail:
	return ret;
}

```

> 주의 사항:
> 1. 명령어 함수가 완성되기 전까지 절대로 `insmod`를 수행하면 안됩니다.
> 2. kvmalloc()으로 할당한 경우에는 kvfree()를 사용해서 반드시 메모리 해제를 해야합니다.

## ramdisk_exit() 구현

ramdisk 모듈을 삭제하는 경우를 위한 부분으로 여태까지 할당했던 내용을 전부 해제하면 됩니다.

```c
static void __exit ramdisk_exit(void)
{
	if (ramdisk_dev == NULL) {
		return;
	}
	if (ramdisk_dev->disk) {
		del_gendisk(ramdisk_dev->disk);
		put_disk(ramdisk_dev->disk);
	}
    if (ramdisk_dev->storage) {
        // 유의 사항: kvmalloc으로 할당한 것은 kvfree()로 해제해야 합니다.
        kvfree(ramdisk_dev->storage);
    }
	unregister_blkdev(major, RAMDISK_DISK_NAME);
	kfree(ramdisk_dev);
	pr_info("ramdisk module is successfully removed\n");
}
```

## ramdisk_queue_rq() 구현

`ramdisk_queue_rq()`가 실제로 MQ Block Layer를 타고 온 요청을 처리하는 부분입니다.

다음과 같이 작성을 해주도록 합니다.

```c
// 실제로 I/O가 처리되는 함수
static blk_status_t ramdisk_queue_rq(struct blk_mq_hw_ctx *hctx,
				     const struct blk_mq_queue_data *bd)
{
	blk_status_t err = BLK_STS_OK;

	// 요청 정보와 ramdisk 구조체를 가져옵니다.
	struct request *rq = bd->rq;
	struct ramdisk_dev_t *ramdisk = hctx->queue->queuedata;

	// 데이터의 위치와 디스크 최대 크기를 가져옵니다.
	loff_t cursor = blk_rq_pos(rq) << SECTOR_SHIFT;
	loff_t disk_size = (ramdisk->capacity << SECTOR_SHIFT);

	struct bio_vec bv;
	struct req_iterator iter;

	blk_mq_start_request(rq);

	// 각 bio vector를 읽어서 명령어에 따라 메모리에 저장하거나 가져오도록 합니다.
	rq_for_each_segment(bv, rq, iter) {
		unsigned int data_size = bv.bv_len;
		void *buf = page_address(bv.bv_page) + bv.bv_offset;

		// 디스크 전체 크기를 넘게 쓰는지를 확인합니다.
		if (cursor + data_size > disk_size) {
			err = BLK_STS_IOERR;
			break;
		}

		switch (req_op(rq)) {
		case REQ_OP_READ:
			memcpy(buf, ramdisk->storage + cursor, data_size);
            break;
		case REQ_OP_WRITE:
			memcpy(ramdisk->storage + cursor, buf, data_size);
            break;
		default:
			err = BLK_STS_IOERR;
			goto exit;
		}
		cursor += data_size;
	}

exit:
	blk_mq_end_request(rq, err);
	return err;
}
```

그리고 Makefile을 실행하고 아래 명령을 통해서 모듈을 적재하고 정상적으로 등록되었는지 확인이 가능합니다.

```bash
sudo insmod ramdisk.ko
sudo dmesg
```

## EXT4 파일 시스템 설치

모듈 적재를 완료하면 `/dev` 디렉터리에 `ramdisk`가 생긴 것을 알 수 있습니다.

해당 디바이스 파일이 우리가 만든 디바이스 드라이버를 사용하는 블록 장치를 나타냅니다.

그러면 해당 블록 장치에 EXT4를 적용을 해보도록 하겠습니다.

```bash
sudo mkfs.ext4 /dev/ramdisk
sudo mkdir -p /mnt/ramdisk
sudo mount /dev/ramdisk /mnt/ramdisk
```

`df -hl` 명령어를 사용하면 정상적으로 마운트가 되었음을 확인할 수 있습니다.

이후에 다음 명령어를 통해 fio를 설치해주도록 합니다.

```bash
sudo apt update -y
sudo apt install -y fio
```

그리고 fio를 실행을 해주도록 합니다.

```bash
sudo fio --name=randomwrite \
    --ioengine=libaio \
    --iodepth=16 \
    --rw=randwrite \
    --size=30M \
    --verify=crc32 \
    --filename=/mnt/ramdisk/randwritedata
```

만약 동작이 정상적으로 끝난다면 ramdisk가 성공적으로 잘 작동하는 것을 알 수 있습니다.
그리고 `df -hl`을 실행하면 디스크 용량이 가득찼음을 확인할 수 있습니다.

## ftrace 사용법

`printk()`를 통한 탐치 방법은 일반적으로 권고하지 않습니다.
특히, 과도한 dmesg 발생은 시스템에 부정적인 영향을 줄 수 있습니다.

이를 대체하기 위해서 `printk()`가 아니라 ftrace를 사용하는 것을 권장합니다.
ftrace의 경우에는 슈퍼 유저 권한으로 진행되어야 하기 때문에 슈퍼 유저로 이동하도록 합니다.

```bash
sudo -s
```

먼저, 사용가능한 트레이스 툴이 어떤 것이 있는지를 확인합니다.

```bash
cat /sys/kernel/tracing/available_tracers
echo 0 > /sys/kernel/tracing/tracing_on # trace 기록을 중지
```
이 중에서 `function_graph`를 사용해보도록 하겠습니다.

```bash
echo function_graph > /sys/kernel/tracing/current_tracer
cat /sys/kernel/tracing/current_tracer # function_graph
```

저희는 ramdisk 함수랑 blk 함수를 추적을 해보도록 하겠습니다.

```bash
echo ramdisk* > /sys/kernel/tracing/set_ftrace_filter
cat set_ftrace_filter # ramdisk_queue_rq가 보입니다.
echo blk* >> /sys/kernel/tracing/set_ftrace_filter
cat set_ftrace_filter # blk_로 시작하는 커널 함수가 추가됩니다.
```

이후에 트레이싱을 활성화해주도록 합니다.

```bash
echo 1 > /sys/kernel/tracing/tracing_on
echo "data" > /mnt/ramdisk/data
cat /sys/kernel/tracing/trace | grep -A 5 -B 5 ramdisk # ramdisk 함수 위 아래 5줄 출력
```

그러면 이와 같은 결과가 나오는 것을 확인할 수 있습니다.

```
 0)               |      blk_mq_do_dispatch_sched() {
 0)   0.290 us    |        blk_req_needs_zone_write_lock();
 0)   0.251 us    |        blk_mq_get_driver_tag();
 0)               |        blk_mq_dispatch_rq_list() {
 0)   0.240 us    |          blk_mq_get_driver_tag();
 0)               |          ramdisk_queue_rq [ramdisk]() {
 0)               |            blk_mq_start_request() {
 0)   0.241 us    |              blk_add_timer();
 0)   0.781 us    |            }
 0)               |            blk_mq_end_request() {
 0)   0.441 us    |              blk_update_request();
```

이를 통해서 어떤 호출이 발생하는지 알 수 있습니다.
모든 것이 완료되면 트레이스를 비활성화를 시켜주도록 합니다.

```bash
echo 0 > /sys/kernel/tracing/tracing_on
echo nop > /sys/kernel/tracing/current_tracer
echo "" > /sys/kernel/tracing/set_ftrace_filter # 모든 함수 트레이싱 활성화
```

그리고 `printk()` 대신에 `trace_printk()`를 사용하면 트레이스 파일에서 출력된 값을 볼 수 있습니다.
`printk()`에 비해 부하도 적으면서 안정적인 출력 상태를 확인할 수 있습니다.

참고로 트레이싱이기 때문에 샘플링이 되어서 결과가 노출됩니다. 따라서, 데이터가 보이지 않는 등의 문제가 발생할 수 있습니다.

## 모든 작업이 끝나면

실행이 완료되었으면 마운트를 해제하고 모듈을 제거하도록 합시다.

```bash
sudo umount /mnt/ramdisk
sudo rmmod ramdisk
```

이 모든 과정에 문제가 없는지 확인하기 위해서 `sudo dmesg`를 수행해보면 됩니다.

## 추가

ftrace 말고도, eBPF 및 Perf와 같은 툴들도 있으니 참고하시면 좋을 것 같습니다. ([링크](https://www.brendangregg.com/blog/2016-12-27/linux-tracing-in-15-minutes.html))

그리고 바로 실행할 수 있는 소스 코드는 `/chapter2/ramdisk`에 존재하니 해당 디렉터리 확인 부탁드립니다.

## 참고 링크

- https://blog.pankajraghav.com/2022/11/30/BLKRAM.html
- https://github.com/Panky-codes/blkram
- https://github.com/rprata/linux-device-driver-tutorial/tree/master
- https://chemnitzer.linux-tage.de/2021/media/programm/folien/165.pdf
- https://docs.kernel.org/block/blk-mq.html
- https://liujunming.top/2019/01/03/Understanding-the-Linux-Kernel-%E8%AF%BB%E4%B9%A6%E7%AC%94%E8%AE%B0-Block-Device-Drivers/
- https://www.kernel.org/doc/html/v5.0/trace/ftrace.html
- https://lwn.net/Articles/322666/
- https://lwn.net/Articles/370423/
