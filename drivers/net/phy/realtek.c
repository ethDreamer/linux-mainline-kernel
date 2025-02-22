// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/realtek.c
 *
 * Driver for Realtek PHYs
 *
 * Author: Johnson Leung <r58129@freescale.com>
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 */
#include <linux/bitops.h>
#include <linux/phy.h>
#include <linux/module.h>

#define RTL821x_PHYSR				0x11
#define RTL821x_PHYSR_DUPLEX			BIT(13)
#define RTL821x_PHYSR_SPEED			GENMASK(15, 14)

#define RTL821x_INER				0x12
#define RTL8211B_INER_INIT			0x6400
#define RTL8211E_INER_LINK_STATUS		BIT(10)
#define RTL8211E_INER_ANEG_COMPLETED		BIT(11)
#define RTL8211E_INER_PAGE_RECEIVED		BIT(12)
#define RTL8211E_INER_ANEG_ERROR		BIT(15)
#define RTL8211F_INER_LINK_STATUS		BIT(4)
#define RTL8211F_INER_PHY_REGISTER_ACCESSIBLE	BIT(5)
#define RTL8211F_INER_WOL_PME			BIT(7)
#define RTL8211F_INER_ALDPS_STATE_CHANGE	BIT(9)
#define RTL8211F_INER_JABBER			BIT(10)

#define RTL821x_INSR				0x13

#define RTL821x_EXT_PAGE_SELECT			0x1e
#define RTL821x_PAGE_SELECT			0x1f

#define RTL8211F_INSR				0x1d

#define RTL8211F_TX_DELAY			BIT(8)
#define RTL8211E_TX_DELAY			BIT(1)
#define RTL8211E_RX_DELAY			BIT(2)
#define RTL8211E_MODE_MII_GMII			BIT(3)

#define RTL8201F_ISR				0x1e
#define RTL8201F_IER				0x13

#define RTL8211F_INTBCR				0x16
#define RTL8211F_INTBCR_INTB_PMEB		BIT(5)

#define RTL8366RB_POWER_SAVE			0x15
#define RTL8366RB_POWER_SAVE_ON			BIT(12)

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Johnson Leung");
MODULE_LICENSE("GPL");

static int rtl821x_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, RTL821x_PAGE_SELECT);
}

static int rtl821x_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, RTL821x_PAGE_SELECT, page);
}

static int rtl8201_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL8201F_ISR);

	return (err < 0) ? err : 0;
}

static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}

static int rtl8211f_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read_paged(phydev, 0xa43, RTL8211F_INSR);

	return (err < 0) ? err : 0;
}

static int rtl8201_config_intr(struct phy_device *phydev)
{
	u16 val;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		val = BIT(13) | BIT(12) | BIT(11);
	else
		val = 0;

	return phy_write_paged(phydev, 0x7, RTL8201F_IER, val);
}

static int rtl8211b_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL8211B_INER_INIT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211e_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL8211E_INER_LINK_STATUS);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}

static int rtl8211f_config_intr(struct phy_device *phydev)
{
	int err;
	u16 val;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/*
		 * The interrupt pin has two functions:
		 * 0: INTB: it acts as interrupt pin which can be configured
		 *    through RTL821x_INER and the status can be read through
		 *    RTL8211F_INSR
		 * 1: PMEB: a special "Power Management Event" mode for
		 *    Wake-on-LAN operation (with support for a "pulse low"
		 *    wave format). Interrupts configured through RTL821x_INER
		 *    will not work in this mode
		 *
		 * select INTB mode in the "INTB pin control" register to
		 * ensure that the interrupt pin is in the correct mode.
		 */
		err = phy_modify_paged(phydev, 0xd40, RTL8211F_INTBCR,
					      RTL8211F_INTBCR_INTB_PMEB, 0);
		if (err)
			return err;

		val = RTL8211F_INER_LINK_STATUS;
	} else {
		val = 0;
	}

	return phy_write_paged(phydev, 0xa42, RTL821x_INER, val);
}

static int rtl8211_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = genphy_config_aneg(phydev);
	if (ret < 0)
		return ret;

	/* Quirk was copied from vendor driver. Unfortunately it includes no
	 * description of the magic numbers.
	 */
	if (phydev->speed == SPEED_100 && phydev->autoneg == AUTONEG_DISABLE) {
		phy_write(phydev, 0x17, 0x2138);
		phy_write(phydev, 0x0e, 0x0260);
	} else {
		phy_write(phydev, 0x17, 0x2108);
		phy_write(phydev, 0x0e, 0x0000);
	}

	return 0;
}

static int rtl8211c_config_init(struct phy_device *phydev)
{
	/* RTL8211C has an issue when operating in Gigabit slave mode */
	return phy_set_bits(phydev, MII_CTRL1000,
			    CTL1000_ENABLE_MASTER | CTL1000_AS_MASTER);
}

static int rtl8211f_config_init(struct phy_device *phydev)
{
	u16 val;

	/* enable TX-delay for rgmii-{id,txid}, and disable it for rgmii and
	 * rgmii-rxid. The RX-delay can be enabled by the external RXDLY pin.
	 */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = RTL8211F_TX_DELAY;
		break;
	default: /* the rest of the modes imply leaving delay as is. */
		return 0;
	}

	return phy_modify_paged(phydev, 0xd08, 0x11, RTL8211F_TX_DELAY, val);
}

static int rtl8211e_config_init(struct phy_device *phydev)
{
	int ret = 0, oldpage;
	u16 val;

	/* enable TX/RX delay for rgmii-* modes, and disable them for rgmii. */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		val = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		val = RTL8211E_TX_DELAY | RTL8211E_RX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val = RTL8211E_RX_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = RTL8211E_TX_DELAY;
		break;
	default: /* the rest of the modes imply leaving delays as is. */
		return 0;
	}

	/* According to a sample driver there is a 0x1c config register on the
	 * 0xa4 extension page (0x7) layout. It can be used to disable/enable
	 * the RX/TX delays otherwise controlled by RXDLY/TXDLY pins. It can
	 * also be used to customize the whole configuration register:
	 * 8:6 = PHY Address, 5:4 = Auto-Negotiation, 3 = Interface Mode Select,
	 * 2 = RX Delay, 1 = TX Delay, 0 = SELRGV (see original PHY datasheet
	 * for details).
	 */
	oldpage = phy_select_page(phydev, 0x7);
	if (oldpage < 0)
		goto err_restore_page;

	ret = __phy_write(phydev, RTL821x_EXT_PAGE_SELECT, 0xa4);
	if (ret)
		goto err_restore_page;

	ret = __phy_modify(phydev, 0x1c, RTL8211E_TX_DELAY | RTL8211E_RX_DELAY,
			   val);

err_restore_page:
	return phy_restore_page(phydev, oldpage, ret);
}

static int rtl8211b_suspend(struct phy_device *phydev)
{
	phy_write(phydev, MII_MMD_DATA, BIT(9));

	return genphy_suspend(phydev);
}

static int rtl8211b_resume(struct phy_device *phydev)
{
	phy_write(phydev, MII_MMD_DATA, 0);

	return genphy_resume(phydev);
}

static int rtl8366rb_config_init(struct phy_device *phydev)
{
	int ret;

	ret = phy_set_bits(phydev, RTL8366RB_POWER_SAVE,
			   RTL8366RB_POWER_SAVE_ON);
	if (ret) {
		dev_err(&phydev->mdio.dev,
			"error enabling power management\n");
	}

	return ret;
}

static struct phy_driver realtek_drvs[] = {
	{
		PHY_ID_MATCH_EXACT(0x00008201),
		.name           = "RTL8201CP Ethernet",
	}, {
		PHY_ID_MATCH_EXACT(0x001cc816),
		.name		= "RTL8201F Fast Ethernet",
		.ack_interrupt	= &rtl8201_ack_interrupt,
		.config_intr	= &rtl8201_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc910),
		.name		= "RTL8211 Gigabit Ethernet",
		.config_aneg	= rtl8211_config_aneg,
		.read_mmd	= &genphy_read_mmd_unsupported,
		.write_mmd	= &genphy_write_mmd_unsupported,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc912),
		.name		= "RTL8211B Gigabit Ethernet",
		.ack_interrupt	= &rtl821x_ack_interrupt,
		.config_intr	= &rtl8211b_config_intr,
		.read_mmd	= &genphy_read_mmd_unsupported,
		.write_mmd	= &genphy_write_mmd_unsupported,
		.suspend	= rtl8211b_suspend,
		.resume		= rtl8211b_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc913),
		.name		= "RTL8211C Gigabit Ethernet",
		.config_init	= rtl8211c_config_init,
		.read_mmd	= &genphy_read_mmd_unsupported,
		.write_mmd	= &genphy_write_mmd_unsupported,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc914),
		.name		= "RTL8211DN Gigabit Ethernet",
		.ack_interrupt	= rtl821x_ack_interrupt,
		.config_intr	= rtl8211e_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc915),
		.name		= "RTL8211E Gigabit Ethernet",
		.config_init	= &rtl8211e_config_init,
		.ack_interrupt	= &rtl821x_ack_interrupt,
		.config_intr	= &rtl8211e_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc916),
		.name		= "RTL8211F Gigabit Ethernet",
		.config_init	= &rtl8211f_config_init,
		.ack_interrupt	= &rtl8211f_ack_interrupt,
		.config_intr	= &rtl8211f_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc800),
		.name		= "Generic Realtek PHY",
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= rtl821x_read_page,
		.write_page	= rtl821x_write_page,
	}, {
		PHY_ID_MATCH_EXACT(0x001cc961),
		.name		= "RTL8366RB Gigabit Ethernet",
		.config_init	= &rtl8366rb_config_init,
		/* These interrupts are handled by the irq controller
		 * embedded inside the RTL8366RB, they get unmasked when the
		 * irq is requested and ACKed by reading the status register,
		 * which is done by the irqchip code.
		 */
		.ack_interrupt	= genphy_no_ack_interrupt,
		.config_intr	= genphy_no_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	},
};

module_phy_driver(realtek_drvs);

static const struct mdio_device_id __maybe_unused realtek_tbl[] = {
	{ PHY_ID_MATCH_VENDOR(0x001cc800) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, realtek_tbl);
