#!/usr/bin/env python3
"""i2c-mt65xx (mainline 6.16) -> i2c-mt65xx-mch: support for the MT6785 multi-channel I2C (camera buses 2/4/7).

Vendor (kernel-r-oss, drivers/i2c/busses/i2c-mtk.c, ch_offset_default = <0x100>):
  * configuration (SOFTRESET, IO_CONFIG, DCM_EN, TIMING, LTIMING, HS) -> the shadow registers of channel 0 (base);
  * transaction registers (address, interrupts, lengths, START, FIFO...) -> the channel at base + 0x100;
  * FIFO_ADDR_CLR |= FIFO_ADDR_CLR_MCH (0x4); MCU_INTR (0x40) = 1 before START;
  * ACKERR/HS_NACKERR are not enabled in the channel interrupt mask (the status is set anyway);
  * after an error/timeout: init_hw + RESUME_ARBIT (0x2) into the shadow START.
A separate compatible "xiaomi,mt6785-i2c-mch", so that the built-in i2c-mt65xx does not take these nodes.
usage: mch-port.py <mainline i2c-mt65xx.c> <out.c>
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
s = open(src, encoding='utf-8').read()


def sub(old, new, count=1, regex=False):
    global s
    n = len(re.findall(old, s)) if regex else s.count(old)
    assert n == count, '%r: %d matches, expected %d' % (old[:70], n, count)
    s = re.sub(old, new, s) if regex else s.replace(old, new)


sub('#define I2C_DRV_NAME\t\t"i2c-mt65xx"', '#define I2C_DRV_NAME\t\t"i2c-mt65xx-mch"')

sub('\tvoid __iomem *base;\t\t/* i2c base addr */\n',
    '\tvoid __iomem *base;\t\t/* i2c base addr */\n'
    '\tu32 ch_off;\t\t\t/* MT6785: AP channel offset (mediatek,ch-offset), 0 = a normal controller */\n')

sub('''static u16 mtk_i2c_readw(struct mtk_i2c *i2c, enum I2C_REGS_OFFSET reg)
{
	return readw(i2c->base + i2c->dev_comp->regs[reg]);
}''', '''/* MT6785 multi-channel: the configuration lives in channel 0 (shadow), the transactions - in the AP channel */
#define I2C_FIFO_ADDR_CLR_MCH	0x0004
#define I2C_RESUME_ARBIT_MCH	0x0002
#define I2C_MCU_INTR_MCH	0x40
#define I2C_MCU_INTR_EN_MCH	0x0001

static bool mtk_i2c_is_shadow(enum I2C_REGS_OFFSET reg)
{
	switch (reg) {
	case OFFSET_SOFTRESET:
	case OFFSET_IO_CONFIG:
	case OFFSET_DCM_EN:
	case OFFSET_TIMING:
	case OFFSET_LTIMING:
	case OFFSET_HS:
		return true;
	default:
		return false;
	}
}

static void __iomem *mtk_i2c_reg(struct mtk_i2c *i2c, enum I2C_REGS_OFFSET reg)
{
	return i2c->base + (mtk_i2c_is_shadow(reg) ? 0 : i2c->ch_off) + i2c->dev_comp->regs[reg];
}

static u16 mtk_i2c_readw(struct mtk_i2c *i2c, enum I2C_REGS_OFFSET reg)
{
	return readw(mtk_i2c_reg(i2c, reg));
}''')

sub('''	writew(val, i2c->base + i2c->dev_comp->regs[reg]);
}''', '''	writew(val, mtk_i2c_reg(i2c, reg));
	/* experiment: duplicate the configuration into the AP channel too (DCM_EN 0xf88 is outside the channel window - not touched) */
	if (i2c->ch_off && mch_cfg_both && mtk_i2c_is_shadow(reg) && reg != OFFSET_DCM_EN)
		writew(val, i2c->base + i2c->ch_off + i2c->dev_comp->regs[reg]);
}

static void mtk_i2c_mch_dump(struct mtk_i2c *i2c, const char *why)
{
	void __iomem *c = i2c->base + i2c->ch_off, *b = i2c->base;

	dev_info(i2c->dev, "mch dump (%s): ch start 0x%x stat 0x%x mask 0x%x ctrl 0x%x dbgst 0x%x fifo 0x%x err 0x%x mcu 0x%x | "
		 "ch0 start 0x%x stat 0x%x mask 0x%x dbgst 0x%x | dma en 0x%x\\n", why,
		 readw(c + 0x24), readw(c + 0x0c), readw(c + 0x08), readw(c + 0x10), readw(c + 0xe4),
		 readw(c + 0xf4), readw(c + 0x84), readw(c + 0x40),
		 readw(b + 0x24), readw(b + 0x0c), readw(b + 0x08), readw(b + 0xe4),
		 readl(i2c->pdmabase + OFFSET_EN));
}

/* after an error the channel must be released: RESUME_ARBIT into START of channel 0 (like the vendor) */
static void mtk_i2c_mch_resume(struct mtk_i2c *i2c)
{
	if (!i2c->ch_off)
		return;
	writew(I2C_RESUME_ARBIT_MCH, i2c->base + i2c->dev_comp->regs[OFFSET_START]);
	if (mch_resume_ch)	/* experiment: also into START of the AP channel itself */
		writew(I2C_RESUME_ARBIT_MCH, i2c->base + i2c->ch_off + i2c->dev_comp->regs[OFFSET_START]);
	if (mch_debug)
		dev_info(i2c->dev, "mch: resume -> start ch0 0x%x ch 0x%x\\n",
			 readw(i2c->base + i2c->dev_comp->regs[OFFSET_START]),
			 readw(i2c->base + i2c->ch_off + i2c->dev_comp->regs[OFFSET_START]));
}''')

# match table: only our compatible
s_new = re.sub(r'static const struct of_device_id mtk_i2c_of_match\[\] = \{.*?\n\};',
               'static const struct of_device_id mtk_i2c_of_match[] = {\n'
               '\t{ .compatible = "xiaomi,mt6785-i2c-mch", .data = &mt8183_compat },\n'
               '\t{}\n};', s, count=1, flags=re.S)
assert s_new != s
s = s_new

sub('\tmtk_i2c_writew(i2c, I2C_FIFO_ADDR_CLR, OFFSET_FIFO_ADDR_CLR);\n',
    '\tmtk_i2c_writew(i2c, I2C_FIFO_ADDR_CLR | (i2c->ch_off ? I2C_FIFO_ADDR_CLR_MCH : 0),\n'
    '\t\t       OFFSET_FIFO_ADDR_CLR);\n')

sub('''	/* Enable interrupt */
	mtk_i2c_writew(i2c, restart_flag | I2C_HS_NACKERR | I2C_ACKERR |
			    I2C_ARB_LOST | I2C_TRANSAC_COMP, OFFSET_INTR_MASK);''',
    '''	/* Enable interrupt (in the MCH channel the vendor does not enable ACKERR - the status is set anyway) */
	mtk_i2c_writew(i2c, restart_flag | I2C_ARB_LOST | I2C_TRANSAC_COMP |
			    (i2c->ch_off ? 0 : (I2C_HS_NACKERR | I2C_ACKERR)),
		       OFFSET_INTR_MASK);''')

sub('\tmtk_i2c_writew(i2c, start_reg, OFFSET_START);\n',
    '\t/* MCU_INTR: route the channel interrupt to the CPU (the vendor always writes it for v2; mainline - never) */\n'
    '\twritew(I2C_MCU_INTR_EN_MCH, i2c->base + i2c->ch_off + I2C_MCU_INTR_MCH);\n'
    '\tmtk_i2c_writew(i2c, start_reg, OFFSET_START);\n')

sub('''		i2c_dump_register(i2c);
		mtk_i2c_init_hw(i2c);
		return -ETIMEDOUT;''', '''		i2c_dump_register(i2c);
		if (i2c->ch_off)
			mtk_i2c_mch_dump(i2c, "timeout");
		mtk_i2c_init_hw(i2c);
		mtk_i2c_mch_resume(i2c);
		return -ETIMEDOUT;''')

sub('''	if (i2c->irq_stat & (I2C_HS_NACKERR | I2C_ACKERR)) {
		dev_dbg(i2c->dev, "addr: %x, transfer ACK error\\n", msgs->addr);
		mtk_i2c_init_hw(i2c);
		return -ENXIO;''', '''	if (i2c->ch_off && mch_debug)
		dev_info(i2c->dev, "mch: addr 0x%x irq_stat 0x%x err 0x%x dbg 0x%x start 0x%x\\n", msgs->addr,
			 i2c->irq_stat, readw(i2c->base + i2c->ch_off + I2C_ERROR_MCH),
			 readw(i2c->base + i2c->ch_off + 0xe4), readw(i2c->base + i2c->ch_off + 0x24));
	/* MCH channel: errors = ACKERR/HS_NACKERR/BUS_ERR/TIMEOUT (the vendor checks all of them) */
	if (i2c->irq_stat & (I2C_HS_NACKERR | I2C_ACKERR |
			     (i2c->ch_off ? (I2C_BUS_ERR_MCH | I2C_TIMEOUT_MCH) : 0))) {
		dev_dbg(i2c->dev, "addr: %x, transfer ACK error\\n", msgs->addr);
		/* vendor: the channel transaction completed (COMP) without BUS_ERR -> do NOT reset the channel,
		 * only clear the FIFO (MCH|CLR) and return the error. A soft reset + RESUME_ARBIT here jams the channel. */
		if (i2c->ch_off && mch_nack_mode == 0 && (i2c->irq_stat & I2C_TRANSAC_COMP) &&
		    !(i2c->irq_stat & I2C_BUS_ERR_MCH)) {
			u16 st0 = readw(i2c->base + i2c->ch_off + i2c->dev_comp->regs[OFFSET_START]);

			mtk_i2c_writew(i2c, I2C_FIFO_ADDR_CLR | I2C_FIFO_ADDR_CLR_MCH,
				       OFFSET_FIFO_ADDR_CLR);
			/* after a NACK the channel is paused (START = START|RESUME_ARBIT): continue/release */
			if (mch_nack_resume)
				writew(I2C_RESUME_ARBIT_MCH,
				       i2c->base + i2c->ch_off + i2c->dev_comp->regs[OFFSET_START]);
			/* mainline resets the AP-DMA only in init_hw; without a reset the next start hangs */
			if (mch_debug)
				dev_info(i2c->dev, "mch: NACK start 0x%x -> 0x%x dma_en 0x%x\\n", st0,
					 readw(i2c->base + i2c->ch_off + i2c->dev_comp->regs[OFFSET_START]),
					 readl(i2c->pdmabase + OFFSET_EN));
			if (mch_dma_rst) {
				writel(I2C_DMA_WARM_RST, i2c->pdmabase + OFFSET_RST);
				udelay(10);
				writel(I2C_DMA_CLR_FLAG, i2c->pdmabase + OFFSET_RST);
				if (mch_debug)
					dev_info(i2c->dev, "mch: dma warm rst -> en 0x%x\\n",
						 readl(i2c->pdmabase + OFFSET_EN));
			}
			return -ENXIO;
		}
		mtk_i2c_init_hw(i2c);
		if (mch_nack_mode != 1)
			mtk_i2c_mch_resume(i2c);
		if (i2c->ch_off && mch_debug)
			mtk_i2c_mch_dump(i2c, "after NACK/init_hw");
		return -ENXIO;''')

# channel interrupt mask like the vendor for v2: + BUS_ERR + TIMEOUT
sub('''			    (i2c->ch_off ? 0 : (I2C_HS_NACKERR | I2C_ACKERR)),''',
    '''			    (i2c->ch_off ? (I2C_BUS_ERR_MCH | I2C_TIMEOUT_MCH |
					    (mch_ackmask ? (I2C_HS_NACKERR | I2C_ACKERR) : 0)) :
					   (I2C_HS_NACKERR | I2C_ACKERR)),''')

sub('#define I2C_MCU_INTR_EN_MCH\t0x0001\n',
    '#define I2C_MCU_INTR_EN_MCH\t0x0001\n'
    '#define I2C_BUS_ERR_MCH\t\t(1 << 8)\n'
    '#define I2C_TIMEOUT_MCH\t\t(1 << 5)\n'
    '#define I2C_ERROR_MCH\t\t0x84\n\n'
    'static bool mch_debug;\n'
    'module_param(mch_debug, bool, 0644);\n'
    'MODULE_PARM_DESC(mch_debug, "print irq_stat of every MCH channel transaction");\n'
    '/* ACKERR in the channel mask BREAKS NACK: the handler clears ACKERR in the middle of the NACK handling, the channel gets stuck. The vendor does not enable it. */\n'
    'static bool mch_ackmask;\n'
    'module_param(mch_ackmask, bool, 0644);\n'
    'MODULE_PARM_DESC(mch_ackmask, "include ACKERR in the channel interrupt mask (the vendor does not)");\n'
    'static int mch_nack_act = 0x01;\n'
    'module_param(mch_nack_act, int, 0644);\n'
    'MODULE_PARM_DESC(mch_nack_act, "PIO NACK: 1 FIFO clr, 2 RESUME ch, 4 RESUME ch0, 8 SRST ch, 0x10 SRST ch0, 0x20 HS+SRST ch, 0x40 START=0 ch, 0x80 SRST clr");\n'
    'static bool mch_pio = true;\n'
    'module_param(mch_pio, bool, 0644);\n'
    'MODULE_PARM_DESC(mch_pio, "MCH channel: short transactions through the FIFO without DMA (like the vendor)");\n'
    'static bool mch_reinit = true;\n'
    'module_param(mch_reinit, bool, 0644);\n'
    'MODULE_PARM_DESC(mch_reinit, "MCH channel: init_hw before every transaction (like the vendor)");\n'
    'static int mch_nack_mode;\n'
    'module_param(mch_nack_mode, int, 0644);\n'
    'MODULE_PARM_DESC(mch_nack_mode, "0 vendor (FIFO clr), 1 init_hw, 2 init_hw+RESUME_ARBIT");\n'
    'static bool mch_dma_rst = true;\n'
    'module_param(mch_dma_rst, bool, 0644);\n'
    'static bool mch_autorestart;\n'
    'module_param(mch_autorestart, bool, 0644);\n'
    'static bool mch_nack_resume = true;\n'
    'module_param(mch_nack_resume, bool, 0644);\n'
    'static int mch_chan = -1;\n'
    'module_param(mch_chan, int, 0444);\n'
    'MODULE_PARM_DESC(mch_chan, "-1 = channel offset from DT; otherwise forced (0 = channel 0)");\n'
    'static bool mch_resume_ch = true;\n'
    'module_param(mch_resume_ch, bool, 0644);\n'
    'static bool mch_cfg_both = true;\n'
    'module_param(mch_cfg_both, bool, 0444);\n'
    'MODULE_PARM_DESC(mch_cfg_both, "write the configuration (TIMING/HS/IO_CONFIG/SOFTRESET) to both channel 0 and the AP channel");\n')

sub('''	ret = of_property_read_u32(np, "clock-frequency", &i2c->speed_hz);''',
    '''	of_property_read_u32(np, "mediatek,ch-offset", &i2c->ch_off);
	if (mch_chan >= 0)	/* experiment: override the channel (0 = channel 0) */
		i2c->ch_off = mch_chan;

	ret = of_property_read_u32(np, "clock-frequency", &i2c->speed_hz);''')

# MCH channel: before EVERY transaction the vendor rewrites the channel configuration (TIMING/LTIMING/HS/IO_CONFIG/
# CLOCK_DIV/EXT_CONF/CONTROL/DELAY_LEN/HW_TIMEOUT) - after a transaction the channel registers apparently get reset.
sub('\ti2c->irq_stat = 0;\n\n\tif (i2c->auto_restart)\n',
    '\tif (i2c->ch_off && mch_reinit)\n'
    '\t\tmtk_i2c_init_hw(i2c);\n'
    '\ti2c->irq_stat = 0;\n\n\tif (i2c->auto_restart)\n')

# MCH channel: the vendor writes only TRANSAC_START into START (no RS_MUL_TRIG/CNFG) - auto-restart is disabled in the channel
sub('\ti2c->auto_restart = i2c->dev_comp->auto_restart;\n',
    '\ti2c->auto_restart = i2c->dev_comp->auto_restart;\n'
    '\tif (i2c->ch_off && !mch_autorestart)\n'
    '\t\ti2c->auto_restart = 0;\n')

# ---- PIO path of the MCH channel (like the vendor: short transactions through the FIFO, without AP-DMA) ----
PIO = r'''
/*
 * MT6785 multi-channel: transactions <= 8 bytes - through the channel FIFO, without AP-DMA (the vendor does the same;
 * mainline always enables DMA, and after the very first transaction the channel gets stuck waiting for the DMA handshake).
 * Supported: one write, one read, write+read (WRRD) to one address.
 */
static bool mtk_i2c_mch_pio_ok(struct i2c_msg *msgs, int num)
{
	if (num == 1)
		return msgs[0].len && msgs[0].len <= 8;
	return num == 2 && !(msgs[0].flags & I2C_M_RD) && (msgs[1].flags & I2C_M_RD) &&
	       msgs[0].addr == msgs[1].addr && msgs[0].len && msgs[0].len <= 8 &&
	       msgs[1].len && msgs[1].len <= 8;
}

static int mtk_i2c_mch_pio(struct mtk_i2c *i2c, struct i2c_msg *msgs, int num)
{
	bool wrrd = num == 2;
	struct i2c_msg *rm = wrrd ? &msgs[1] : ((msgs[0].flags & I2C_M_RD) ? &msgs[0] : NULL);
	u16 ctrl, i;
	unsigned long left;

	/* channel configuration - before every transaction (like the vendor) */
	mtk_i2c_writew(i2c, I2C_IO_CONFIG_OPEN_DRAIN, OFFSET_IO_CONFIG);
	if (i2c->dev_comp->timing_adjust) {
		mtk_i2c_writew(i2c, i2c->ac_timing.htiming, OFFSET_TIMING);
		mtk_i2c_writew(i2c, i2c->ac_timing.ltiming, OFFSET_LTIMING);
		mtk_i2c_writew(i2c, i2c->ac_timing.hs, OFFSET_HS);
		mtk_i2c_writew(i2c, i2c->ac_timing.inter_clk_div, OFFSET_CLOCK_DIV);
		mtk_i2c_writew(i2c, i2c->ac_timing.ext, OFFSET_EXT_CONF);
	} else {
		mtk_i2c_writew(i2c, i2c->timing_reg, OFFSET_TIMING);
		mtk_i2c_writew(i2c, i2c->ltiming_reg, OFFSET_LTIMING);
		mtk_i2c_writew(i2c, i2c->high_speed_reg, OFFSET_HS);
	}
	mtk_i2c_writew(i2c, I2C_DELAY_LEN, OFFSET_DELAY_LEN);

	ctrl = I2C_CONTROL_ACKERR_DET_EN | I2C_CONTROL_CLK_EXT_EN;
	if (wrrd)
		ctrl |= I2C_CONTROL_DIR_CHANGE | I2C_CONTROL_RS;
	mtk_i2c_writew(i2c, ctrl, OFFSET_CONTROL);
	mtk_i2c_writew(i2c, i2c_8bit_addr_from_msg(msgs), OFFSET_SLAVE_ADDR);
	mtk_i2c_writew(i2c, 0x1ff, OFFSET_INTR_STAT);
	mtk_i2c_writew(i2c, I2C_FIFO_ADDR_CLR | I2C_FIFO_ADDR_CLR_MCH, OFFSET_FIFO_ADDR_CLR);
	mtk_i2c_writew(i2c, I2C_TRANSAC_COMP | I2C_ARB_LOST | I2C_BUS_ERR_MCH | I2C_TIMEOUT_MCH |
			    (mch_ackmask ? (I2C_HS_NACKERR | I2C_ACKERR) : 0), OFFSET_INTR_MASK);
	if (wrrd) {
		mtk_i2c_writew(i2c, msgs[0].len, OFFSET_TRANSFER_LEN);
		mtk_i2c_writew(i2c, msgs[1].len, OFFSET_TRANSFER_LEN_AUX);
		mtk_i2c_writew(i2c, I2C_WRRD_TRANAC_VALUE, OFFSET_TRANSAC_LEN);
	} else {
		mtk_i2c_writew(i2c, msgs[0].len, OFFSET_TRANSFER_LEN);
		mtk_i2c_writew(i2c, 1, OFFSET_TRANSAC_LEN);
	}
	if (!(msgs[0].flags & I2C_M_RD))
		for (i = 0; i < msgs[0].len; i++)
			mtk_i2c_writew(i2c, msgs[0].buf[i], OFFSET_DATA_PORT);

	writew(I2C_MCU_INTR_EN_MCH, i2c->base + i2c->ch_off + I2C_MCU_INTR_MCH);
	i2c->irq_stat = 0;
	reinit_completion(&i2c->msg_complete);
	mb();
	mtk_i2c_writew(i2c, I2C_TRANSAC_START, OFFSET_START);
	left = wait_for_completion_timeout(&i2c->msg_complete, i2c->adap.timeout);
	mtk_i2c_writew(i2c, 0, OFFSET_INTR_MASK);

	if (mch_debug)
		dev_info(i2c->dev, "mch pio: addr 0x%x n%d irq_stat 0x%x start 0x%x\n", msgs[0].addr, num,
			 i2c->irq_stat, readw(i2c->base + i2c->ch_off + i2c->dev_comp->regs[OFFSET_START]));
	if (!left) {
		mtk_i2c_mch_dump(i2c, "pio timeout");
		mtk_i2c_writew(i2c, I2C_FIFO_ADDR_CLR | I2C_FIFO_ADDR_CLR_MCH, OFFSET_FIFO_ADDR_CLR);
		mtk_i2c_init_hw(i2c);
		mtk_i2c_mch_resume(i2c);
		return -ETIMEDOUT;
	}
	if (i2c->irq_stat & (I2C_HS_NACKERR | I2C_ACKERR | I2C_BUS_ERR_MCH | I2C_TIMEOUT_MCH)) {
		void __iomem *c = i2c->base + i2c->ch_off, *b = i2c->base;
		u16 st = readw(c + 0x24);

		/* recovery after a NACK - the set of actions from mch_nack_act (a bit mask, experiment) */
		if (mch_nack_act & 0x01)
			mtk_i2c_writew(i2c, I2C_FIFO_ADDR_CLR | I2C_FIFO_ADDR_CLR_MCH, OFFSET_FIFO_ADDR_CLR);
		if (mch_nack_act & 0x40)
			writew(0, c + 0x24);
		if (mch_nack_act & 0x02)
			writew(I2C_RESUME_ARBIT_MCH, c + 0x24);
		if (mch_nack_act & 0x04)
			writew(I2C_RESUME_ARBIT_MCH, b + 0x24);
		if (mch_nack_act & 0x08)
			writew(I2C_SOFT_RST, c + 0x50);
		if (mch_nack_act & 0x10)
			writew(I2C_SOFT_RST, b + 0x50);
		if (mch_nack_act & 0x20)
			writew(I2C_HANDSHAKE_RST | I2C_SOFT_RST, c + 0x50);
		if (mch_nack_act & 0x80) {
			udelay(50);
			writew(I2C_CHN_CLR_FLAG, c + 0x50);
		}
		if (mch_debug)
			dev_info(i2c->dev, "mch pio NACK act 0x%x: start 0x%x -> ch 0x%x ch0 0x%x\n",
				 mch_nack_act, st, readw(c + 0x24), readw(b + 0x24));
		return -ENXIO;
	}
	if (rm)
		for (i = 0; i < rm->len; i++)
			rm->buf[i] = mtk_i2c_readw(i2c, OFFSET_DATA_PORT) & 0xff;
	return num;
}

'''
sub('static int mtk_i2c_transfer(struct i2c_adapter *adap,\n',
    PIO + 'static int mtk_i2c_transfer(struct i2c_adapter *adap,\n')
sub('\ti2c->auto_restart = i2c->dev_comp->auto_restart;\n\tif (i2c->ch_off && !mch_autorestart)\n',
    '\tif (i2c->ch_off && mch_pio && mtk_i2c_mch_pio_ok(msgs, num)) {\n'
    '\t\tret = mtk_i2c_mch_pio(i2c, msgs, num);\n'
    '\t\tgoto err_exit;\n'
    '\t}\n\n'
    '\ti2c->auto_restart = i2c->dev_comp->auto_restart;\n\tif (i2c->ch_off && !mch_autorestart)\n')

s = s.replace('MODULE_DESCRIPTION("MediaTek I2C Bus Driver");',
              'MODULE_DESCRIPTION("MediaTek I2C Bus Driver + MT6785 multi-channel (begonia camera buses)");')
open(dst, 'w', encoding='utf-8').write(s)
print('ok ->', dst)
