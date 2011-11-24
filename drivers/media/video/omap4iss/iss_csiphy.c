/*
 * iss_csiphy.c
 *
 * TI OMAP4 ISS - CSI PHY module
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *
 * Contacts: Sergio Aguirre <saaguirre@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/delay.h>
#include <linux/device.h>

#include "iss.h"
#include "iss_regs.h"
#include "iss_csiphy.h"

/*
 * csiphy_lanes_config - Configuration of CSIPHY lanes.
 *
 * Updates HW configuration.
 * Called with phy->mutex taken.
 */
static void csiphy_lanes_config(struct iss_csiphy *phy)
{
	unsigned int i;
	u32 reg;

	reg = readl(phy->cfg_regs + CSI2_COMPLEXIO_CFG);

	for (i = 0; i < phy->num_data_lanes; i++) {
		reg &= ~(CSI2_COMPLEXIO_CFG_DATA_POL(i + 1) |
			 CSI2_COMPLEXIO_CFG_DATA_POSITION_MASK(i + 1));
		reg |= (phy->lanes.data[i].pol ?
			CSI2_COMPLEXIO_CFG_DATA_POL(i + 1) : 0);
		reg |= (phy->lanes.data[i].pos <<
			CSI2_COMPLEXIO_CFG_DATA_POSITION_SHIFT(i + 1));
	}

	reg &= ~(CSI2_COMPLEXIO_CFG_CLOCK_POL |
		 CSI2_COMPLEXIO_CFG_CLOCK_POSITION_MASK);
	reg |= phy->lanes.clk.pol ? CSI2_COMPLEXIO_CFG_CLOCK_POL : 0;
	reg |= phy->lanes.clk.pos << CSI2_COMPLEXIO_CFG_CLOCK_POSITION_SHIFT;

	writel(reg, phy->cfg_regs + CSI2_COMPLEXIO_CFG);
}

/*
 * csiphy_set_power
 * @power: Power state to be set.
 *
 * Returns 0 if successful, or -EBUSY if the retry count is exceeded.
 */
static int csiphy_set_power(struct iss_csiphy *phy, u32 power)
{
	u32 reg;
	u8 retry_count;

	writel((readl(phy->cfg_regs + CSI2_COMPLEXIO_CFG) &
		~CSI2_COMPLEXIO_CFG_PWD_CMD_MASK) |
		power,
		phy->cfg_regs + CSI2_COMPLEXIO_CFG);

	retry_count = 0;
	do {
		udelay(50);
		reg = readl(phy->cfg_regs + CSI2_COMPLEXIO_CFG) &
				CSI2_COMPLEXIO_CFG_PWD_STATUS_MASK;

		if (reg != power >> 2)
			retry_count++;

	} while ((reg != power >> 2) && (retry_count < 100));

	if (retry_count == 100) {
		printk(KERN_ERR "CSI2 CIO set power failed!\n");
		return -EBUSY;
	}

	return 0;
}

/*
 * csiphy_dphy_config - Configure CSI2 D-PHY parameters.
 *
 * Called with phy->mutex taken.
 */
static void csiphy_dphy_config(struct iss_csiphy *phy)
{
	u32 reg;

	/* Set up REGISTER0 */
	reg = readl(phy->phy_regs + REGISTER0);

	reg &= ~(REGISTER0_THS_TERM_MASK |
		 REGISTER0_THS_SETTLE_MASK);
	reg |= phy->dphy.ths_term << REGISTER0_THS_TERM_SHIFT;
	reg |= phy->dphy.ths_settle << REGISTER0_THS_SETTLE_SHIFT;

	writel(reg, phy->phy_regs + REGISTER0);

	/* Set up REGISTER1 */
	reg = readl(phy->phy_regs + REGISTER1);

	reg &= ~(REGISTER1_TCLK_TERM_MASK |
		 REGISTER1_CTRLCLK_DIV_FACTOR_MASK |
		 REGISTER1_TCLK_SETTLE_MASK);
	reg |= phy->dphy.tclk_term << REGISTER1_TCLK_TERM_SHIFT;
	reg |= phy->dphy.tclk_miss << REGISTER1_CTRLCLK_DIV_FACTOR_SHIFT;
	reg |= phy->dphy.tclk_settle << REGISTER1_TCLK_SETTLE_SHIFT;

	writel(reg, phy->phy_regs + REGISTER1);
}

static int csiphy_config(struct iss_csiphy *phy,
			 struct iss_csiphy_dphy_cfg *dphy,
			 struct iss_csiphy_lanes_cfg *lanes)
{
	unsigned int used_lanes = 0;
	unsigned int i;

	/* Clock and data lanes verification */
	for (i = 0; i < phy->num_data_lanes; i++) {
		if (lanes->data[i].pos == 0)
			continue;

		if (lanes->data[i].pol > 1 || lanes->data[i].pos > 5)
			return -EINVAL;

		if (used_lanes & (1 << lanes->data[i].pos))
			return -EINVAL;

		used_lanes |= 1 << lanes->data[i].pos;
	}

	if (lanes->clk.pol > 1 || lanes->clk.pos > 4)
		return -EINVAL;

	if (lanes->clk.pos == 0 || used_lanes & (1 << lanes->clk.pos))
		return -EINVAL;

	mutex_lock(&phy->mutex);
	phy->dphy = *dphy;
	phy->lanes = *lanes;
	mutex_unlock(&phy->mutex);

	return 0;
}

int omap4iss_csiphy_acquire(struct iss_csiphy *phy)
{
	int rval;

	mutex_lock(&phy->mutex);

	rval = omap4iss_csi2_reset(phy->csi2);
	if (rval)
		goto done;

	csiphy_dphy_config(phy);
	csiphy_lanes_config(phy);

	rval = csiphy_set_power(phy, CSI2_COMPLEXIO_CFG_PWD_CMD_ON);
	if (rval)
		goto done;

	phy->phy_in_use = 1;

done:
	mutex_unlock(&phy->mutex);
	return rval;
}

void omap4iss_csiphy_release(struct iss_csiphy *phy)
{
	mutex_lock(&phy->mutex);
	if (phy->phy_in_use) {
		csiphy_set_power(phy, CSI2_COMPLEXIO_CFG_PWD_CMD_OFF);
		phy->phy_in_use = 0;
	}
	mutex_unlock(&phy->mutex);
}

/*
 * omap4iss_csiphy_init - Initialize the CSI PHY frontends
 */
int omap4iss_csiphy_init(struct iss_device *iss)
{
	struct iss_csiphy *phy1 = &iss->csiphy1;

	iss->platform_cb.csiphy_config = csiphy_config;

	phy1->iss = iss;
	phy1->csi2 = &iss->csi2a;
	phy1->num_data_lanes = ISS_CSIPHY1_NUM_DATA_LANES;
	phy1->cfg_regs = iss->regs[OMAP4_ISS_MEM_CSI2_A_REGS1];
	phy1->phy_regs = iss->regs[OMAP4_ISS_MEM_CAMERARX_CORE1];
	mutex_init(&phy1->mutex);

	return 0;
}
