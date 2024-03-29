/*
 * Copyright(c) 2018-2021 Allwinnertech Co., Ltd.
 *
 * Hardware dram frequency scaling, for which code run in dram.
 *
 * Author: frank <frank@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#define MDFS_DEBUG 0

/* helper for debug */
#if MDFS_DEBUG
static void reg_dump(struct sunxi_dramfreq *dramfreq)
{
	printk("MC_MDFSCR:%x\n", readl(dramfreq->dramcom_base + MC_MDFSCR));
	printk("VTFCR:%x\n", readl(dramfreq->dramctl_base + VTFCR));
	printk("RFSHTMG:%x\n", readl(dramfreq->dramctl_base + RFSHTMG));
	printk("DXnGCR0(0):%x\n", readl(dramfreq->dramctl_base + DXnGCR0(0)));
	printk("DXnGCR0(1):%x\n", readl(dramfreq->dramctl_base + DXnGCR0(1)));
	printk("DXnGCR0(2):%x\n", readl(dramfreq->dramctl_base + DXnGCR0(2)));
	printk("DXnGCR0(3):%x\n", readl(dramfreq->dramctl_base + DXnGCR0(3)));
	printk("ODTMAP:%x\n", readl(dramfreq->dramctl_base + ODTMAP));
	printk("_DRAM_CLK_REG:%x\n", readl(dramfreq->ccu_base + _DRAM_CLK_REG));
	printk("PGCR0:%x\n", readl(dramfreq->dramctl_base + PGCR0));
}
#endif

/* CONFIG_ARCH_SUN8IW18 */
#if CONFIG_ARCH_SUN8IW18
static int mdfs_dfs(struct sunxi_dramfreq *dramfreq, unsigned int freq)
{
	struct dram_para_t *para = &dramfreq->dram_para;
	unsigned int rank_num;
	uint32_t reg_val;
	unsigned int trefi, trfc, ctrl_freq;
	unsigned int i = 0, n = 4;
	unsigned int div, source;
	unsigned int vtf ;

	//bit0 must be 0 for new MDFS process
	while (readl(dramfreq->dramcom_base + MC_MDFSCR) & 0x1)
		;
	//calculate source and divider
	if (para->dram_tpr9 != 0) {
		if (((para->dram_clk % freq) == 0) && ((para->dram_tpr9 % freq) == 0)) {
			if ((para->dram_clk / freq) > (para->dram_tpr9 / freq)) {
				source = 0;
				div = para->dram_tpr9 / freq;
			} else {
				source = 1;
				div = para->dram_clk / freq;
			}
		} else if ((para->dram_clk % freq) == 0) {
			source = 1;
			div = para->dram_clk / freq;
		} else if ((para->dram_tpr9 % freq) == 0) {
			source = 0;
			div = para->dram_tpr9 / freq;
		} else{
			printk("MDFS fail!\n");
			return 1;
		}
	} else {
		source = (para->dram_tpr13 >> 6) & 0x1;
		div = para->dram_clk / freq;
	}

	ctrl_freq = freq >> 1;

	if ((para->dram_type == 3) || (para->dram_type == 2)) {
		trefi = ((7800 * ctrl_freq) / 1000 + ((((7800 * ctrl_freq) % 1000) != 0) ? 1 : 0)) / 32;
		trfc = (350 * ctrl_freq) / 1000 + ((((350 * ctrl_freq) % 1000) != 0) ? 1 : 0);
	} else {
		trefi = ((3900 * ctrl_freq) / 1000 + ((((3900 * ctrl_freq) % 1000) != 0) ? 1 : 0)) / 32;
		trfc = (210 * ctrl_freq) / 1000 + ((((210 * ctrl_freq) % 1000) != 0) ? 1 : 0);
	}
	/*turn off vtf*/
	vtf = (readl(dramfreq->dramctl_base + VTFCR) >> 8) & 0x3;
	if (vtf) {
		reg_val = readl(dramfreq->dramctl_base + VTFCR);
		reg_val &= ~(0x3 << 8);
		writel(reg_val, dramfreq->dramctl_base + VTFCR);
	}
	/* set dual buffer for timing change and power save */
	reg_val = readl(dramfreq->dramcom_base + MC_MDFSCR);
	/* VTC dual buffer can not be used */
	reg_val |= (0x1U << 15);
	writel(reg_val, dramfreq->dramcom_base + MC_MDFSCR);
	/* change refresh timing */
	reg_val = readl(dramfreq->dramctl_base + RFSHTMG);
	reg_val &= ~((0xfff << 0) | (0xfff << 16));
	reg_val |= ((trfc << 0) | (trefi << 16));
	writel(reg_val, dramfreq->dramctl_base + RFSHTMG);

	/* change ODT status for power save  */
	if (!((para->dram_tpr13 >> 12) & 0x1)) {
		if (freq > 400) {
			if ((para->dram_odt_en & 0x1)) {
				for (i = 0; i < n; i++) {
					//byte 0/byte 1
					reg_val = readl(dramfreq->dramctl_base + DXnGCR0(i));
					reg_val &= ~(0x3U << 4);
					reg_val |= (0x0 << 4); //ODT dynamic
					writel(reg_val, dramfreq->dramctl_base + DXnGCR0(i));
				}
				rank_num = readl(dramfreq->dramcom_base + MC_WORK_MODE) & 0x1;
				if (rank_num) {
					writel(0x303, dramfreq->dramctl_base + ODTMAP);
				} else {
					writel(0x201, dramfreq->dramctl_base + ODTMAP);
				}
			}
		} else {
			if ((para->dram_odt_en & 0x1)) {
				for (i = 0; i < n; i++) {
					//byte 0/byte 1
					reg_val = readl(dramfreq->dramctl_base + DXnGCR0(i));
					reg_val &= ~(0x3U << 4);
					reg_val |= (0x2 << 4);	//ODT off
					writel(reg_val, dramfreq->dramctl_base + DXnGCR0(i));
				}
				writel(0x0, dramfreq->dramctl_base + ODTMAP);
			}
		}
	}

	//set the DRAM_CFG_REG divider in CCMU
	reg_val = readl(dramfreq->ccu_base + _DRAM_CLK_REG);
	reg_val &= ~(0xf << 0);
	reg_val |= ((div - 1) << 0);
	reg_val &= ~(0x3 << 24);
//	reg_val |= (source << 24);
	reg_val |= (0x1 << 24); //AW1821Ö»ÓÐPLL_DDR0£¬Ð´¹Ì¶¨Öµ
	writel(reg_val, dramfreq->ccu_base + _DRAM_CLK_REG);

	/* set MDFS register */
	reg_val = readl(dramfreq->dramcom_base + MC_MDFSCR);
	reg_val |= (0x1 << 4); //bypass
//	reg_val |= (0x1 << 13); //pad hold
	reg_val &= ~(0x1U << 1); //DFS mode
	writel(reg_val, dramfreq->dramcom_base + MC_MDFSCR);

	reg_val = readl(dramfreq->dramcom_base + MC_MDFSCR);
	reg_val |= (0x1U << 0); //start mdfs
	writel(reg_val, dramfreq->dramcom_base + MC_MDFSCR);

	/* wait for process finished */
	while (readl(dramfreq->dramcom_base + MC_MDFSCR) & 0x1)
		;
	/* turn off dual buffer */
	reg_val = readl(dramfreq->dramcom_base + MC_MDFSCR);
	reg_val &= ~(0x1U << 15);
//	reg_val &= ~(0x1 << 13); //pad hold
	writel(reg_val, dramfreq->dramcom_base + MC_MDFSCR);
	/*turn on vtf*/
	if (vtf) {
		reg_val = readl(dramfreq->dramctl_base + VTFCR);
		reg_val |= (0x3<<8);
		writel(reg_val, dramfreq->dramctl_base + VTFCR);
	}

	return 0;
}
#endif /* CONFIG_ARCH_SUN8IW18 */
