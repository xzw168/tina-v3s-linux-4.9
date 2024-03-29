#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/spinlock.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/semaphore.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/mutex.h>
/*#include <mach/clock.h>*/
/*#include <mach/platform.h>*/
/*#include <mach/hardware.h>*/
/*#include <mach/sys_config.h>*/
#include <linux/dma-mapping.h>
/*#include <mach/dma.h>*/
#include <linux/wait.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>
/*#include <mach/gpio.h>*/
#include <linux/gpio.h>

#include "nand_lib.h"

extern int NAND_Print(const char *fmt, ...);

#define  OOB_BUF_SIZE                   32
#define NAND_BOOT0_BLK_START    0
#define NAND_BOOT0_BLK_CNT		2
#define NAND_UBOOT_BLK_START    (NAND_BOOT0_BLK_START+NAND_BOOT0_BLK_CNT)
#define NAND_UBOOT_BLK_CNT		18
#define NAND_BOOT0_PAGE_CNT_PER_COPY     32

#define debug NAND_Print

extern int get_nand_para(void *boot_buf);
extern int gen_uboot_check_sum(void *boot_buf);
extern int gen_check_sum(void *boot_buf);
extern int get_dram_para(void *boot_buf);
extern int get_nand_para_for_boot1(void *boot_buf);

extern int NAND_PhysicLockInit(void);
extern int NAND_PhysicLock(void);
extern int NAND_PhysicUnLock(void);
extern int NAND_PhysicLockExit(void);

extern __u32 NAND_GetPageSize(void);
extern __u32 NAND_GetPageCntPerBlk(void);
extern __u32 NAND_GetVersion(__u8 *nand_version);
extern __u32 NAND_GetBlkCntPerChip(void);
extern __u32 NAND_GetChipCnt(void);
extern __s32 NAND_GetBlkCntOfDie(void);
extern int NAND_IS_Secure_sys(void);

__s32  burn_boot0_1k_mode(__u32 Boot0_buf)
{
	__u32 i, j, k;
	__u32 pages_per_block;
	__u32 copies_per_block;
	__u8  oob_buf[32];
	struct boot_physical_param  para;

	debug("burn boot0 normal mode!\n");

	for (i = 0; i < 32; i++)
		oob_buf[i] = 0xff;

	NAND_GetVersion(oob_buf);
	if ((oob_buf[0] != 0xff) || (oob_buf[1] != 0x00)) {
		debug("get flash driver version error!");
		goto error;
	}

	/* ��� page count */
	pages_per_block = NAND_GetPageCntPerBlk();
	if (pages_per_block % 64) {
		debug("get page cnt per block error %x!\n", pages_per_block);
		goto error;
	}

	/* cal copy cnt per bock */
	copies_per_block = pages_per_block / NAND_BOOT0_PAGE_CNT_PER_COPY;

	/* burn boot0 copys */
	for (i = NAND_BOOT0_BLK_START;  i < (NAND_BOOT0_BLK_START + NAND_BOOT0_BLK_CNT);  i++) {
		debug("boot0 %x \n", i);

		/* ������ */
		para.chip  = 0;
		para.block = i;
		if (PHY_SimpleErase(&para) < 0) {
			debug("Fail in erasing block %d.\n", i);
			/*continue;*/
		}
		debug("after erase.\n");

		/* �ڿ�����дboot0���� */
		for (j = 0;  j < copies_per_block;  j++) {

			for (k = 0;  k < NAND_BOOT0_PAGE_CNT_PER_COPY;  k++) {
				para.chip  = 0;
				para.block = i;
				para.page = j * NAND_BOOT0_PAGE_CNT_PER_COPY + k;
				para.mainbuf = (void *)(Boot0_buf + k * 1024);
				para.oobbuf = oob_buf;
				para.sectorbitmap = 0x3;
				if (PHY_SimpleWrite(&para) < 0) {
					debug("Warning. Fail in writing page %d in block %d.\n", j * NAND_BOOT0_PAGE_CNT_PER_COPY + k, i);
				}
			}
		}
	}
	return 0;

error:
	return -1;
}


int NAND_BurnBoot0(unsigned int length, void *buf)
{
	__u32 page_size;

	NAND_PhysicLock();

	get_nand_para(buf);
	gen_check_sum(buf);

	page_size = NAND_GetPageSize();

	if (burn_boot0_1k_mode((__u32)buf))
		goto error;

	NAND_PhysicUnLock();
	debug("burn boot 0 nonsecure success\n");
	return 0;

error:
	debug("burn boot 0 nonsecure failed\n");
	NAND_PhysicUnLock();
	return -1;
}

__s32  read_boot0_1k_mode(__u32 Boot0_buf)
{
	__u32 i, j, k, m, err_flag;
	__u32 pages_per_block;
	__u32 copies_per_block;
	__u8  oob_buf[32];
	struct boot_physical_param  para;

	debug("read boot0 normal mode!\n");

	/* ��� page count */
	pages_per_block = NAND_GetPageCntPerBlk();
	if (pages_per_block % 64) {
		debug("get page cnt per block error %x!", pages_per_block);
		goto error;
	}

	/* cal copy cnt per bock */
	copies_per_block = pages_per_block / NAND_BOOT0_PAGE_CNT_PER_COPY;

	/* read boot0 copys */
	for (i = NAND_BOOT0_BLK_START;  i < (NAND_BOOT0_BLK_START + NAND_BOOT0_BLK_CNT);  i++) {
		debug("boot0 blk %x \n", i);

		/* ������ */
		para.chip  = 0;
		para.block = i;

		for (j = 0;  j < copies_per_block;  j++) {
			err_flag = 0;
			for (k = 0;  k < NAND_BOOT0_PAGE_CNT_PER_COPY;  k++) {
				para.chip  = 0;
				para.block = i;
				para.page = j * NAND_BOOT0_PAGE_CNT_PER_COPY + k;
				para.mainbuf = (void *)(Boot0_buf + k * 1024);
				para.oobbuf = oob_buf;
				para.sectorbitmap = 0x3;
				for (m = 0; m < 32; m++)
					oob_buf[m] = 0x55;
				if (PHY_SimpleRead(&para) < 0) {
					debug("Warning. Fail in read page %d in block %d.\n", j * NAND_BOOT0_PAGE_CNT_PER_COPY + k, i);
					err_flag = 1;
					break;
				}
				if ((oob_buf[0] != 0xff) || (oob_buf[1] != 0x00)) {
					debug("get flash driver version error!\n");
					err_flag = 1;
					break;
				}
			}
			if (err_flag == 0)
				break;
		}
		if (err_flag == 0)
			break;
	}
	return 0;

error:
	return -1;
}

int NAND_ReadBoot0(unsigned int length, void *buf)
{
	__u32 page_size;
	void *buffer = NULL;

	buffer = (void *)kmalloc(1024 * 64, GFP_KERNEL);
	if (buffer == NULL) {
		debug("no memory!\n");
		return -1;
	}

	page_size = NAND_GetPageSize();

	if (read_boot0_1k_mode((__u32)buffer))
		goto error;

	memcpy(buf, buffer, length);
	kfree(buffer);
	buffer = NULL;
	debug("nand read boot0 nonsecure ok\n");
	return 0;

error:
	kfree(buffer);
	buffer = NULL;
	debug("nand read boot0 nonsecure fail\n");
	return -1;

}

__s32 burn_uboot_in_one_blk(__u32 BOOT1_buf, __u32 length)
{
	__u32 i, k;
	__u8  oob_buf[32];
	__u32 page_size, pages_per_block, pages_per_copy;
	struct boot_physical_param  para;

	debug("burn boot1 in one block!\n");

	for (i = 0; i < 32; i++)
		oob_buf[i] = 0xff;

	/* get nand driver version */
	NAND_GetVersion(oob_buf);
	if ((oob_buf[0] != 0xff) || (oob_buf[1] != 0x00)) {
		debug("get flash driver version error!\n");
		goto error;
	}


	/* ï¿½ï¿½ï¿½ page count */
	page_size = NAND_GetPageSize();
	{
		if (page_size % 1024) {
			debug("get flash page size error!\n");
			goto error;
		}
	}

	/* ï¿½ï¿½ï¿½ page count */
	pages_per_block = NAND_GetPageCntPerBlk();
	if (pages_per_block % 64) {
		debug("get page cnt per block error %x!\n", pages_per_block);
		goto error;
	}

	debug("pages_per_block: 0x%x\n", pages_per_block);

	/*
	ï¿½ï¿½ï¿½ï¿½Ã¿ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï?
	?½ï¿½page */
	if (length % page_size) {
		debug("uboot length check error!\n");
		goto error;
	}
	pages_per_copy = length / page_size;
	if (pages_per_copy > pages_per_block) {
		debug("pages_per_copy check error!\n");
		goto error;
	}

	debug("pages_per_copy: 0x%x\n", pages_per_copy);

	if (page_size == 2048)
		para.sectorbitmap = 0xf;
	else if (page_size == 4096)
		para.sectorbitmap = 0xff;
	else if (page_size == 8192)
		para.sectorbitmap = 0xffff;

	/* burn uboot */
	for (i = NAND_UBOOT_BLK_START;  i < (NAND_UBOOT_BLK_START + NAND_UBOOT_BLK_CNT);  i++) {
		debug("boot1 %x \n", i);

		/* ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ */
		para.chip  = 0;
		para.block = i;
		if (PHY_SimpleErase(&para) < 0) {
			debug("Fail in erasing block %d.\n", i);
			/*continue;*/
		}

		for (k = 0;  k < pages_per_block;  k++) {
			para.chip  = 0;
			para.block = i;
			para.page  = k;
			para.mainbuf = (void *)(BOOT1_buf + k * page_size);
			para.oobbuf = oob_buf;
			if (PHY_SimpleWrite(&para) < 0) {
				debug("Warning. Fail in writing page %d in block %d.\n", k, i);
			}
		}
	}
	return 0;

error:
	return -1;
}

__s32 burn_uboot_in_many_blks(__u32 BOOT1_buf, __u32 length)
{
	__u32 i,  k;
	__u8  oob_buf[32];
	__u32 page_size, pages_per_block, pages_per_copy, blocks_per_copy, block_index;
	struct boot_physical_param  para;

	debug("burn uboot in many blks!\n");

	for (i = 0; i < 32; i++)
		oob_buf[i] = 0xff;

	/* get nand driver version */
	NAND_GetVersion(oob_buf);
	if ((oob_buf[0] != 0xff) || (oob_buf[1] != 0x00)) {
		debug("get flash driver version error!\n");
		goto error;
	}


	/* ï¿½ï¿½ï¿½ page count */
	page_size = NAND_GetPageSize();
	{
		if (page_size % 1024) {
			debug("get flash page size error!\n");
			goto error;
		}
	}

	/* ï¿½ï¿½ï¿½ page count */
	pages_per_block = NAND_GetPageCntPerBlk();
	if (pages_per_block % 64) {
		debug("get page cnt per block error %x!\n", pages_per_block);
		goto error;
	}

	pages_per_copy = length / page_size;
	if (length % page_size) {
		pages_per_copy++;
	}
	if (pages_per_copy <= pages_per_block) {
		debug("pages_per_copy check error!\n");
		goto error;
	}

	blocks_per_copy = pages_per_copy / pages_per_block;
	if (pages_per_copy % pages_per_block) {
		blocks_per_copy++;
	}

	if (page_size == 2048)
		para.sectorbitmap = 0xf;
	else if (page_size == 4096)
		para.sectorbitmap = 0xff;
	else if (page_size == 8192)
		para.sectorbitmap = 0xffff;

	/* burn uboot */
	block_index = 0;
	for (i = NAND_UBOOT_BLK_START;  i < (NAND_UBOOT_BLK_START + NAND_UBOOT_BLK_CNT);  i++) {
		debug("uboot %x \n", i);

		/* ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ */
		para.chip  = 0;
		para.block = i;
		if (PHY_SimpleErase(&para) < 0) {
			debug("Fail in erasing block %d.\n", i);
			continue;
		}

		/* ï¿½Ú¿ï¿½ï¿½ï¿½ï¿½ï¿½Ð´boot0ï¿½ï¿½ï¿½ï¿½, lsb modeï¿½Â£ï¿½Ã¿ï¿½ï¿½ï¿½ï¿½Ö»ï¿½ï¿½Ð´Ç°4ï¿½ï¿½page */
		for (k = 0;  k < pages_per_block;  k++) {
			para.chip  = 0;
			para.block = i;
			para.page  = k;
			para.mainbuf = (void *)(BOOT1_buf + (block_index * pages_per_block + k) * page_size);
			para.oobbuf = oob_buf;
			if (PHY_SimpleWrite(&para) < 0) {
				debug("Warning. Fail in writing page %d in block %d.\n", k, i);
			}
		}

		block_index++;
		if (block_index >= blocks_per_copy)
			block_index = 0;

	}

	return 0;

error:
	return -1;
}

int NAND_BurnBoot1(unsigned int length, void *buf)
{
	int ret = 0;
	__u32 page_size, pages_per_block, block_size;

	NAND_PhysicLock();

	page_size = NAND_GetPageSize();
	{
		if (page_size % 1024) {
			debug("get flash page size error!\n");
			goto error;
		}
	}

	pages_per_block = NAND_GetPageCntPerBlk();
	if (pages_per_block % 64) {
		debug("get page cnt per block error %x!\n", pages_per_block);
		goto error;
	}

	block_size = page_size * pages_per_block;
	if (length % page_size) {
		debug("uboot length check error!\n");
		goto error;
	}

	if (length <= block_size) {
		ret = burn_uboot_in_one_blk((__u32)buf, length);
		debug("NAND_BurnBoot1, in one blk, %d\n", ret);
	} else {
		ret = burn_uboot_in_many_blks((__u32)buf, length);
		debug("NAND_BurnBoot1, in many blks, %d\n", ret);
	}

	NAND_PhysicUnLock();
	debug("burn boot 1 success\n");
	return ret;

error:
	NAND_PhysicUnLock();
	debug("burn boot 1 failed\n");
	return -1;
}

void test_dram_para(void *buffer)
{
	int *data;
	int  i;

	data = (int *)buffer;
	for (i = 0; i < 40; i += 4) {
		debug("%x %x %x %x\n", data[i + 0], data[i + 1], data[i + 2], data[i + 3]);
	}
	debug("\n");

	return;
}

__u32  PHY_erase_chip(void)
{
	struct boot_physical_param  para_read;
	int  i, j, k;
	int  ret;
	uint  bad_block_flag;
	uint  chip_cnt, page_size, page_per_block, blk_cnt_per_chip;
	uint block_cnt_of_die, start_blk;
	int  page_index[4];
	uint  chip;
	unsigned char   oob_buf_read[OOB_BUF_SIZE];
	unsigned char  *page_buf_read;
	int  error_flag = 0;

	page_buf_read = (unsigned char *)kmalloc(16 * 1024, GFP_KERNEL);
	if (!page_buf_read) {
		debug("malloc memory for page read fail\n");
		return -1;
	}
	debug("Ready to erase chip.\n");

	page_size = NAND_GetPageSize();
	page_per_block = NAND_GetPageCntPerBlk();
	blk_cnt_per_chip = NAND_GetBlkCntPerChip();
	debug("page_size=%d, page_per_block=%d, blk_cnt_per_chip=%d\n", page_size, page_per_block, blk_cnt_per_chip);
	chip_cnt = NAND_GetChipCnt();
	debug("chip_cnt = %x\n", chip_cnt);
	block_cnt_of_die = NAND_GetBlkCntOfDie();

	page_index[0] = 0;
	page_index[1] = 0xEE;
	page_index[2] = 0xEE;
	page_index[3] = 0xEE;


	for (i = 0;  i < chip_cnt;  i++) {
		/*select chip*/
		chip = i;
		debug("erase chip %u \n", chip);

		start_blk = 0;

		/*scan for bad blocks, only erase good block, all 0x00 blocks is defined bad blocks*/
		for (j = start_blk;  j < blk_cnt_per_chip;  j++) {

			para_read.block = j;

			if ((j & 0xff) == 0)
				debug("erase chip %u, block %u\n", chip, para_read.block);

			para_read.chip = chip;
			para_read.mainbuf = page_buf_read;
			para_read.oobbuf = oob_buf_read;

			bad_block_flag = 0;

			for (k = 0; k < 4; k++) {
				para_read.page = page_index[k];
				if (para_read.page == 0xEE)
					break;

				ret = PHY_SimpleRead(&para_read);

				/*check the current block is a all 0x00 block*/
				if (oob_buf_read[0] == 0x0) {
					bad_block_flag = 1;
					debug("find a bad block %u\n", para_read.block);
					break;
				}

			}

			if (bad_block_flag)
				continue;


			ret = PHY_SimpleErase(&para_read);
			if (ret != 0) {
				debug("erasing block %u failed.\n", para_read.block);
			}
		}
	}
	debug("has cleared the chip.\n");
	if (error_flag)
		debug("the nand is Bad.\n");
	else
		debug("the nand is OK.\n");

	kfree(page_buf_read);

	return 0;

}

__s32 NAND_Get_OTA_Version(void)
{
	return 0x1;
}

