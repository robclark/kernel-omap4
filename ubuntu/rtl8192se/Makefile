NIC_SELECT = RTL8192SE


EXTRA_CFLAGS = -DRTL8192SE
EXTRA_CFLAGS += -DEEPROM_OLD_FORMAT_SUPPORT=0 		\
		-DRTL92SE_FPGA_VERIFY=0 		\
		-DDEMO_BOARD_SUPPORT=1 			\
		-DRTL8192SU_DISABLE_CCK_RATE=0 		\
		-DRTL8192S_DISABLE_FW_DM=0 		\
		-DHAL_MAC_ENABLE=1 			\
		-DHAL_BB_ENABLE=1 			\
		-DHAL_RF_ENABLE=1 			\
		-DRTL8192S_PREPARE_FOR_NORMAL_RELEASE=1 \
		-DDISABLE_BB_RF=0 			\
		-DCONFIG_PM_RTL 			\
		-DENABLE_GPIO_RADIO_CTL 		\
		-DRTL8190_Download_Firmware_From_Header=1 \
		-DCONFIG_PM 				\
		-DENABLE_LPS 				\
		-DCONFIG_ASPM_OR_D3 			\
		-DCONFIG_RX_CMD				\
		-DENABLE_IPS 				\
		-DUSE_FW_SOURCE_IMG_FILE 		\
		-DENABLE_DOT11D 			\
		-DCONFIG_FW_SETCHAN			

EXTRA_CFLAGS += -I$(PWD)/ubuntu/rtl8192se
EXTRA_CFLAGS += -std=gnu89
EXTRA_CFLAGS += -O2
EXTRA_CFLAGS += -mhard-float -DCONFIG_FORCE_HARD_FLOAT=y
EXTRA_CFLAGS += -DTHOMAS_TURBO
#flag for single module compile
EXTRA_CFLAGS += -DBUILT_IN_RTLLIB

r8192se_pci-objs := rtl_core.o 		\
		  rtl_regd.o		\
		  rtl_eeprom.o 	\
		  rtl_wx.o 		\
		  rtl_cam.o		\
		  rtl_pm.o 		\
		  rtl_ps.o 		\
		  rtl_dm.o 		\
		  rtl_debug.o		\
		  rtl_ethtool.o         \
		  rtl8192s/r8192S_dev.o 	\
		  rtl8192s/r8192S_Efuse.o 	\
		  rtl8192s/r8192S_phy.o 	\
		  rtl8192s/r8192S_firmware.o 	\
		  rtl8192s/r8192S_rtl6052.o 	\
		  rtl8192s/r8192S_hwimg.o 	\
		  rtl8192s/r8192S_led.o 	\
		  rtl8192s/r8192S_mp.o		\
		  rtl8192s/r8192S_scan.o	\
		  rtllib/rtllib_rx.o                \
		  rtllib/rtllib_softmac.o           \
		  rtllib/rtllib_tx.o                \
		  rtllib/rtllib_wx.o                \
		  rtllib/rtllib_module.o            \
		  rtllib/rtllib_softmac_wx.o        \
		  rtllib/rtl819x_HTProc.o              \
		  rtllib/rtl819x_TSProc.o              \
		  rtllib/rtl819x_BAProc.o              \
		  rtllib/dot11d.o                      \
		  rtllib/rtllib_crypt.o             \
		  rtllib/rtllib_crypt_tkip.o        \
		  rtllib/rtllib_crypt_ccmp.o        \
		  rtllib/rtllib_crypt_wep.o		\
		  rtllib/wapi.o        			\
		  rtllib/wapi_interface.o

obj-$(CONFIG_RTL8192SE) := r8192se_pci.o
