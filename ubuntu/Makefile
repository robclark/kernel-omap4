#
# Makefile for the Linux kernel ubuntu supplied third-party device drivers.
#

##
##
##
obj-$(CONFIG_BLK_DEV_COMPCACHE)	+= compcache/
##
##
##
obj-$(CONFIG_DM_RAID45)		+= dm-raid4-5/
##
##
##
obj-$(CONFIG_NDISWRAPPER)	+= ndiswrapper/
##
##
##
obj-m				+= rfkill/
##
##
##
obj-$(CONFIG_FSAM7400)		+= fsam7400/
##
##
##
obj-$(CONFIG_OMNIBOOK)		+= omnibook/
##
##
##
obj-$(CONFIG_SCSI_ISCSITARGET)	+= iscsitarget/
##
##
##
obj-$(CONFIG_RTL8192SE)		+= rtl8192se/
##
##
##
obj-$(CONFIG_AUFS_FS)		+= aufs/
##
##
##
##
##
##

# This is a stupid trick to get kbuild to create ubuntu/built-in.o
obj- += foo.o
