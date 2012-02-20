build_arch	= powerpc
header_arch	= $(build_arch)
asm_link	= $(build_arch)
defconfig	= pmac32_defconfig
flavours	= powerpc powerpc-smp powerpc64-smp
build_image	= vmlinux
kernel_file	= $(build_image)
install_file	= $(build_image)

loader		= yaboot

custom_flavours	=

no_dumpfile = true
skipdbg		= true
skipabi		= true
skipmodule	= true

family=ubuntu
