/*
 * iss_csiphy.h
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

#ifndef OMAP4_ISS_CSI_PHY_H
#define OMAP4_ISS_CSI_PHY_H

struct iss_csi2_device;

struct csiphy_lane {
	u8 pos;
	u8 pol;
};

#define ISS_CSIPHY1_NUM_DATA_LANES	4

struct iss_csiphy_lanes_cfg {
	struct csiphy_lane data[ISS_CSIPHY1_NUM_DATA_LANES];
	struct csiphy_lane clk;
};

struct iss_csiphy_dphy_cfg {
	u8 ths_term;
	u8 ths_settle;
	u8 tclk_term;
	unsigned tclk_miss:1;
	u8 tclk_settle;
};

struct iss_csiphy {
	struct iss_device *iss;
	struct mutex mutex;	/* serialize csiphy configuration */
	u8 phy_in_use;
	struct iss_csi2_device *csi2;

	/* Pointer to register remaps into kernel space */
	void __iomem *cfg_regs;
	void __iomem *phy_regs;

	u8 num_data_lanes;	/* number of CSI2 Data Lanes supported */
	struct iss_csiphy_lanes_cfg lanes;
	struct iss_csiphy_dphy_cfg dphy;
};

int omap4iss_csiphy_acquire(struct iss_csiphy *phy);
void omap4iss_csiphy_release(struct iss_csiphy *phy);
int omap4iss_csiphy_init(struct iss_device *iss);

#endif	/* OMAP4_ISS_CSI_PHY_H */
