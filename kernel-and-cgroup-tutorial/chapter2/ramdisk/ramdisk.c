#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/genhd.h>
#include <linux/types.h>
#include <linux/idr.h>
#include <linux/numa.h>
#include <linux/mm.h>

#define RAMDISK_DISK_NAME "ramdisk"
#define RAMDISK_CAPACITY_BYTES (40 << 20) // 40MiB
#define RAMDISK_MAX_SEGMENTS (32)
#define RAMDISK_MAX_SEGMENT_SIZE (64 << 10) // 64KiB

struct ramdisk_dev_t {
	sector_t capacity;
	u8 *storage;

	struct blk_mq_tag_set tag_set;

	struct gendisk *disk;
};

static int major;
static DEFINE_IDA(ramdisk_indices); // to retrieve the minor id
static struct ramdisk_dev_t *ramdisk_dev = NULL;

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

// ramdisk를 위한 MQ Block Layer를 위한 명령어 집합
static const struct blk_mq_ops ramdisk_mq_ops = {
	.queue_rq = &ramdisk_queue_rq,
};

// ramdisk를 위한 device 명령을 위한 명령어 집합
static const struct block_device_operations ramdisk_rq_ops = {
	.owner = THIS_MODULE,
};

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
	kvfree(ramdisk_dev->storage);
fail_free_ramdisk_dev:
	kfree(ramdisk_dev);
fail:
	return ret;
}

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

module_init(ramdisk_init);
module_exit(ramdisk_exit);

MODULE_AUTHOR("BlaCkinkGJ");
MODULE_LICENSE("GPL");
