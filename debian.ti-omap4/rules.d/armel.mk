build_arch	= arm
header_arch	= arm
asm_link	= arm
defconfig	= defconfig
flavours	= omap4
build_image	= zImage
kernel_file	= arch/$(build_arch)/boot/zImage
install_file	= vmlinuz
no_dumpfile = true

loader		= grub

do_doc_package          = false
do_source_package       = false
do_common_headers_indep = false
do_libc_dev_package     = false
