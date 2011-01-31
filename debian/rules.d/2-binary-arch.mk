# We don't want make removing intermediary stamps
.SECONDARY :

# Prepare the out-of-tree build directory
ifeq ($(do_full_source),true)
build_cd = cd $(builddir)/build-$*; #
build_O  =
else
build_cd =
build_O  = O=$(builddir)/build-$*
endif

prepare-%: $(stampdir)/stamp-prepare-%
	@# Empty for make to be happy
$(stampdir)/stamp-prepare-%: $(stampdir)/stamp-prepare-tree-% prepare-checks-%
	@touch $@
$(stampdir)/stamp-prepare-tree-%: target_flavour = $*
$(stampdir)/stamp-prepare-tree-%: $(commonconfdir)/config.common.$(family) $(archconfdir)/config.common.$(arch) $(archconfdir)/config.flavour.%
	@echo "Preparing $*..."
	install -d $(builddir)/build-$*
	touch $(builddir)/build-$*/ubuntu-build
	[ "$(do_full_source)" != 'true' ] && true || \
		rsync -a --exclude debian --exclude debian.master --exclude $(DEBIAN) * $(builddir)/build-$*
	cat $^ | sed -e 's/.*CONFIG_VERSION_SIGNATURE.*/CONFIG_VERSION_SIGNATURE="Ubuntu $(release)-$(revision)-$* $(release)$(extraversion)"/' > $(builddir)/build-$*/.config
	find $(builddir)/build-$* -name "*.ko" | xargs rm -f
	$(build_cd) $(kmake) $(build_O) silentoldconfig prepare scripts
	touch $@

# Do the actual build, including image and modules
build-%: $(stampdir)/stamp-build-%
	@# Empty for make to be happy
$(stampdir)/stamp-build-%: target_flavour = $*
$(stampdir)/stamp-build-%: prepare-%
	@echo "Building $*..."
	$(build_cd) $(kmake) $(build_O) $(conc_level) $(build_image)
	$(build_cd) $(kmake) $(build_O) $(conc_level) modules
	@touch $@

# Install the finished build
install-%: pkgdir = $(CURDIR)/debian/$(bin_pkg_name)-$*
install-%: bindoc = $(pkgdir)/usr/share/doc/$(bin_pkg_name)-$*
install-%: dbgpkgdir = $(CURDIR)/debian/$(bin_pkg_name)-$*-dbgsym
install-%: basepkg = $(hdrs_pkg_name)
install-%: hdrdir = $(CURDIR)/debian/$(basepkg)-$*/usr/src/$(basepkg)-$*
install-%: target_flavour = $*
install-%: $(stampdir)/stamp-build-% checks-%
	dh_testdir
	dh_testroot
	dh_clean -k -p$(bin_pkg_name)-$*
	dh_clean -k -p$(hdrs_pkg_name)-$*
	dh_clean -k -p$(dbg_pkg_name)-$*

	# The main image
	# compress_file logic required because not all architectures
	# generate a zImage automatically out of the box
ifeq ($(compress_file),)
	install -m644 -D $(builddir)/build-$*/$(kernel_file) \
		$(pkgdir)/boot/$(install_file)-$(abi_release)-$*
else
	install -d $(pkgdir)/boot
	gzip -c9v $(builddir)/build-$*/$(kernel_file) > \
		$(pkgdir)/boot/$(install_file)-$(abi_release)-$*
	chmod 644 $(pkgdir)/boot/$(install_file)-$(abi_release)-$*
endif

	install -m644 $(builddir)/build-$*/.config \
		$(pkgdir)/boot/config-$(abi_release)-$*
	install -m644 $(abidir)/$* \
		$(pkgdir)/boot/abi-$(abi_release)-$*
	install -m600 $(builddir)/build-$*/System.map \
		$(pkgdir)/boot/System.map-$(abi_release)-$*
ifeq ($(no_dumpfile),)
	makedumpfile -g $(pkgdir)/boot/vmcoreinfo-$(abi_release)-$* \
		-x $(builddir)/build-$*/vmlinux
	chmod 0600 $(pkgdir)/boot/vmcoreinfo-$(abi_release)-$*
endif

	$(build_cd) $(kmake) $(build_O) modules_install \
		INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=$(pkgdir)/ \
		INSTALL_FW_PATH=$(pkgdir)/lib/firmware/$(abi_release)-$*

	#
	# Remove all modules not in the inclusion list.
	#
	if [ -f $(DEBIAN)/control.d/$(target_flavour).inclusion-list ] ; then \
		$(SHELL) $(DROOT)/scripts/module-inclusion $(pkgdir)/lib/modules/$(abi_release)-$*/kernel \
			$(DEBIAN)/control.d/$(target_flavour).inclusion-list 2>&1 | \
				tee $(target_flavour).inclusion-list.log; \
		/sbin/depmod -b $(pkgdir) -ea -F $(pkgdir)/boot/System.map-$(abi_release)-$* \
			$(abi_release)-$* 2>&1 |tee $(target_flavour).depmod.log; \
	fi

ifeq ($(no_dumpfile),)
	makedumpfile -g $(pkgdir)/boot/vmcoreinfo-$(abi_release)-$* \
		-x $(builddir)/build-$*/vmlinux
	chmod 0600 $(pkgdir)/boot/vmcoreinfo-$(abi_release)-$*
endif
	rm -f $(pkgdir)/lib/modules/$(abi_release)-$*/build
	rm -f $(pkgdir)/lib/modules/$(abi_release)-$*/source

	# Some initramfs-tools specific modules
	install -d $(pkgdir)/lib/modules/$(abi_release)-$*/initrd
	if [ -f $(pkgdir)/lib/modules/$(abi_release)-$*/kernel/drivers/video/vesafb.ko ]; then\
	  ln -f $(pkgdir)/lib/modules/$(abi_release)-$*/kernel/drivers/video/vesafb.ko \
		$(pkgdir)/lib/modules/$(abi_release)-$*/initrd/; \
	fi

	# Now the image scripts
	install -d $(pkgdir)/DEBIAN
	for script in postinst postrm preinst prerm; do				\
	  sed -e 's/=V/$(abi_release)-$*/g' -e 's/=K/$(install_file)/g'		\
	      -e 's/=L/$(loader)/g'         -e 's@=B@$(build_arch)@g'		\
	       $(DROOT)/control-scripts/$$script > $(pkgdir)/DEBIAN/$$script;	\
	  chmod 755 $(pkgdir)/DEBIAN/$$script;					\
	done

	# Install the full changelog.
ifeq ($(do_doc_package),true)
	install -d $(bindoc)
	cat $(DEBIAN)/changelog $(DEBIAN)/changelog.historical | \
		gzip -9 >$(bindoc)/changelog.Debian.old.gz
	chmod 644 $(bindoc)/changelog.Debian.old.gz
endif

ifneq ($(skipsub),true)
	for sub in $($(*)_sub); do					\
		if ! (TO=$$sub FROM=$* ABI_RELEASE=$(abi_release) $(SHELL)		\
			$(DROOT)/scripts/sub-flavour); then exit 1; fi;		\
		/sbin/depmod -b debian/$(bin_pkg_name)-$$sub		\
			-ea -F debian/$(bin_pkg_name)-$$sub/boot/System.map-$(abi_release)-$* \
			$(abi_release)-$*;					\
		install -d debian/$(bin_pkg_name)-$$sub/DEBIAN;	\
		for script in postinst postrm preinst prerm; do			\
			sed -e 's/=V/$(abi_release)-$*/g'			\
			    -e 's/=K/$(install_file)/g'				\
			    -e 's/=L/$(loader)/g'				\
			    -e 's@=B@$(build_arch)@g'				\
				$(DROOT)/control-scripts/$$script >		\
				debian/$(bin_pkg_name)-$$sub/DEBIAN/$$script;\
			chmod 755  debian/$(bin_pkg_name)-$$sub/DEBIAN/$$script;\
		done;								\
	done
endif

ifneq ($(skipdbg),true)
	# Debug image is simple
	install -m644 -D $(builddir)/build-$*/vmlinux \
		$(dbgpkgdir)/usr/lib/debug/boot/vmlinux-$(abi_release)-$*
	$(build_cd) $(kmake) $(build_O) modules_install \
		INSTALL_MOD_PATH=$(dbgpkgdir)/usr/lib/debug
	rm -f $(dbgpkgdir)/usr/lib/debug/lib/modules/$(abi_release)-$*/build
	rm -f $(dbgpkgdir)/usr/lib/debug/lib/modules/$(abi_release)-$*/source
	rm -f $(dbgpkgdir)/usr/lib/debug/lib/modules/$(abi_release)-$*/modules.*
	rm -fr $(dbgpkgdir)/usr/lib/debug/lib/firmware
endif

	# The flavour specific headers image
	# TODO: Would be nice if we didn't have to dupe the original builddir
	install -d -m755 $(hdrdir)
	cat $(builddir)/build-$*/.config | \
		sed -e 's/.*CONFIG_DEBUG_INFO=.*/# CONFIG_DEBUG_INFO is not set/g' > \
		$(hdrdir)/.config
	chmod 644 $(hdrdir)/.config
	$(kmake) O=$(hdrdir) silentoldconfig prepare scripts
	# We'll symlink this stuff
	rm -f $(hdrdir)/Makefile
	rm -rf $(hdrdir)/include2
	# powerpc seems to need some .o files for external module linking. Add them in.
ifeq ($(arch),powerpc)
	mkdir -p $(hdrdir)/arch/powerpc/lib
	cp $(builddir)/build-$*/arch/powerpc/lib/*.o $(hdrdir)/arch/powerpc/lib
endif
	# Script to symlink everything up
	$(SHELL) $(DROOT)/scripts/link-headers "$(hdrdir)" "$(basepkg)" "$*"
	# Setup the proper asm symlink
	rm -f $(hdrdir)/include/asm
	ln -s asm-$(asm_link) $(hdrdir)/include/asm
	# The build symlink
	install -d debian/$(basepkg)-$*/lib/modules/$(abi_release)-$*
	ln -s /usr/src/$(basepkg)-$* \
		debian/$(basepkg)-$*/lib/modules/$(abi_release)-$*/build
	# And finally the symvers
	install -m644 $(builddir)/build-$*/Module.symvers \
		$(hdrdir)/Module.symvers

	# Now the header scripts
	install -d $(CURDIR)/debian/$(basepkg)-$*/DEBIAN
	for script in postinst; do						\
	  sed -e 's/=V/$(abi_release)-$*/g' -e 's/=K/$(install_file)/g'	\
		$(DROOT)/control-scripts/headers-$$script > 			\
			$(CURDIR)/debian/$(basepkg)-$*/DEBIAN/$$script;		\
	  chmod 755 $(CURDIR)/debian/$(basepkg)-$*/DEBIAN/$$script;		\
	done

	# At the end of the package prep, call the tests
	DPKG_ARCH="$(arch)" KERN_ARCH="$(build_arch)" FLAVOUR="$*"	\
	 VERSION="$(abi_release)" REVISION="$(revision)"		\
	 PREV_REVISION="$(prev_revision)" ABI_NUM="$(abinum)"		\
	 PREV_ABI_NUM="$(prev_abinum)" BUILD_DIR="$(builddir)/build-$*"	\
	 INSTALL_DIR="$(pkgdir)" SOURCE_DIR="$(CURDIR)"			\
	 run-parts -v $(DROOT)/tests

	#
	# Remove files which are generated at installation by postinst,
	# except for modules.order and modules.builtin
	# 
	# NOTE: need to keep this list in sync with postrm
	#
	mkdir $(pkgdir)/lib/modules/$(abi_release)-$*/_
	mv $(pkgdir)/lib/modules/$(abi_release)-$*/modules.order \
		$(pkgdir)/lib/modules/$(abi_release)-$*/_
	if [ -f $(pkgdir)/lib/modules/$(abi_release)-$*/modules.builtin ] ; then \
	    mv $(pkgdir)/lib/modules/$(abi_release)-$*/modules.builtin \
		$(pkgdir)/lib/modules/$(abi_release)-$*/_; \
	fi
	rm -f $(pkgdir)/lib/modules/$(abi_release)-$*/modules.*
	mv $(pkgdir)/lib/modules/$(abi_release)-$*/_/* \
		$(pkgdir)/lib/modules/$(abi_release)-$*
	rmdir $(pkgdir)/lib/modules/$(abi_release)-$*/_

headers_tmp := $(CURDIR)/debian/tmp-headers
headers_dir := $(CURDIR)/debian/linux-libc-dev

hmake := $(MAKE) -C $(CURDIR) O=$(headers_tmp) SUBLEVEL=$(SUBLEVEL) \
	EXTRAVERSION=-$(abinum) INSTALL_HDR_PATH=$(headers_tmp)/install \
	SHELL="$(SHELL)" ARCH=$(header_arch)

install-arch-headers:
	dh_testdir
	dh_testroot
	dh_clean -k -plinux-libc-dev

	rm -rf $(headers_tmp)
	install -d $(headers_tmp) $(headers_dir)/usr/include/

	$(hmake) $(defconfig)
	mv $(headers_tmp)/.config $(headers_tmp)/.config.old
	sed -e 's/^# \(CONFIG_MODVERSIONS\) is not set$$/\1=y/' \
	  -e 's/.*CONFIG_LOCALVERSION_AUTO.*/# CONFIG_LOCALVERSION_AUTO is not set/' \
	  $(headers_tmp)/.config.old > $(headers_tmp)/.config
	$(hmake) silentoldconfig
	$(hmake) headers_install

	( cd $(headers_tmp)/install/include/ && \
		find . -name '.' -o -name '.*' -prune -o -print | \
                cpio -pvd --preserve-modification-time \
			$(headers_dir)/usr/include/ )

	rm -rf $(headers_tmp)

binary-arch-headers: install-arch-headers
	dh_testdir
	dh_testroot
ifeq ($(do_libc_dev_package),true)
ifneq ($(DEBIAN),debian.master)
	echo "non-master branch building linux-libc-dev, aborting"
	exit 1
endif
	dh_installchangelogs -plinux-libc-dev
	dh_installdocs -plinux-libc-dev
	dh_compress -plinux-libc-dev
	dh_fixperms -plinux-libc-dev
	dh_installdeb -plinux-libc-dev
	dh_gencontrol -plinux-libc-dev -- $(libc_dev_version)
	dh_md5sums -plinux-libc-dev
	dh_builddeb -plinux-libc-dev
endif

binary-%: pkgimg = $(bin_pkg_name)-$*
binary-%: pkghdr = $(hdrs_pkg_name)-$*
binary-%: dbgpkg = $(bin_pkg_name)-$*-dbgsym
binary-%: dbgpkgdir = $(CURDIR)/debian/$(bin_pkg_name)-$*-dbgsym
binary-%: install-%
	dh_testdir
	dh_testroot

	dh_installchangelogs -p$(pkgimg)
	dh_installdocs -p$(pkgimg)
	dh_compress -p$(pkgimg)
	dh_fixperms -p$(pkgimg) -X/boot/
	dh_installdeb -p$(pkgimg)
	dh_shlibdeps -p$(pkgimg)
	dh_gencontrol -p$(pkgimg)
	dh_md5sums -p$(pkgimg)
	dh_builddeb -p$(pkgimg) -- -Zbzip2 -z9

	dh_installchangelogs -p$(pkghdr)
	dh_installdocs -p$(pkghdr)
	dh_compress -p$(pkghdr)
	dh_fixperms -p$(pkghdr)
	dh_shlibdeps -p$(pkghdr)
	dh_installdeb -p$(pkghdr)
	dh_gencontrol -p$(pkghdr)
	dh_md5sums -p$(pkghdr)
	dh_builddeb -p$(pkghdr)

ifneq ($(skipsub),true)
	@set -e; for sub in $($(*)_sub); do		\
		pkg=$(bin_pkg_name)-$$sub;	\
		dh_installchangelogs -p$$pkg;		\
		dh_installdocs -p$$pkg;			\
		dh_compress -p$$pkg;			\
		dh_fixperms -p$$pkg -X/boot/;		\
		dh_shlibdeps -p$$pkg;			\
		dh_installdeb -p$$pkg;			\
		dh_gencontrol -p$$pkg;			\
		dh_md5sums -p$$pkg;			\
		dh_builddeb -p$$pkg;			\
	done
endif

ifneq ($(skipdbg),true)
	dh_installchangelogs -p$(dbgpkg)
	dh_installdocs -p$(dbgpkg)
	dh_compress -p$(dbgpkg)
	dh_fixperms -p$(dbgpkg)
	dh_installdeb -p$(dbgpkg)
	dh_gencontrol -p$(dbgpkg)
	dh_md5sums -p$(dbgpkg)
	dh_builddeb -p$(dbgpkg)

	# Hokay...here's where we do a little twiddling...
	# Renaming the debug package prevents it from getting into
	# the primary archive, and therefore prevents this very large
	# package from being mirrored. It is instead, through some
	# archive admin hackery, copied to http://ddebs.ubuntu.com.
	#
	mv ../$(dbgpkg)_$(release)-$(revision)_$(arch).deb \
		../$(dbgpkg)_$(release)-$(revision)_$(arch).ddeb
	set -e; \
	if grep -qs '^Build-Debug-Symbols: yes$$' /CurrentlyBuilding; then \
		sed -i '/^$(dbgpkg)_/s/\.deb /.ddeb /' debian/files; \
	else \
		grep -v '^$(dbgpkg)_.*$$' debian/files > debian/files.new; \
		mv debian/files.new debian/files; \
	fi
	# Now, the package wont get into the archive, but it will get put
	# into the debug system.
endif
ifneq ($(full_build),false)
	# Clean out this flavours build directory.
	rm -rf $(builddir)/build-$*
	# Clean out the debugging package source directory.
	rm -rf $(dbgpkgdir)
endif

$(stampdir)/stamp-flavours:
	@echo $(flavours) > $@

#
# per-architecture packages
#
$(stampdir)/stamp-prepare-perarch:
	@echo "Preparing perarch ..."
ifeq ($(do_tools),true)
	rm -rf $(builddir)/tools-$*
	install -d $(builddir)/tools-$*
	for i in *; do ln -s $(CURDIR)/$$i $(builddir)/tools-$*/; done
	rm $(builddir)/tools-$*/tools
	rsync -a tools/ $(builddir)/tools-$*/tools/
endif
	touch $@

$(stampdir)/stamp-build-perarch: prepare-perarch
ifeq ($(do_tools),true)
	cd $(builddir)/tools-$*/tools/perf && make $(CROSS_COMPILE)
endif
	@touch $@

install-perarch: toolspkgdir = $(CURDIR)/debian/$(tools_pkg_name)
install-perarch: $(stampdir)/stamp-build-perarch
	# Add the tools.
ifeq ($(do_tools),true)
	install -d $(toolspkgdir)/usr/bin
	install -s -m755 $(builddir)/tools-$*/tools/perf/perf \
		$(toolspkgdir)/usr/bin/perf_$(abi_release)
endif

binary-perarch: toolspkg = $(tools_pkg_name)
binary-perarch: install-perarch
	@# Empty for make to be happy
ifeq ($(do_tools),true)
	dh_installchangelogs -p$(toolspkg)
	dh_installdocs -p$(toolspkg)
	dh_compress -p$(toolspkg)
	dh_fixperms -p$(toolspkg)
	dh_shlibdeps -p$(toolspkg)
	dh_installdeb -p$(toolspkg)
	dh_gencontrol -p$(toolspkg)
	dh_md5sums -p$(toolspkg)
	dh_builddeb -p$(toolspkg)
endif

binary-debs: binary-perarch $(stampdir)/stamp-flavours $(addprefix binary-,$(flavours))

build-arch-deps-$(do_flavour_image_package) += $(addprefix build-,$(flavours))
build-arch: $(build-arch-deps-true)

binary-arch-deps-$(do_flavour_image_package) = binary-debs
ifeq ($(AUTOBUILD),)
binary-arch-deps-$(do_flavour_image_package) += binary-udebs
endif
binary-arch-deps-$(do_libc_dev_package) += binary-arch-headers
ifneq ($(do_common_headers_indep),true)
binary-arch-deps-$(do_flavour_header_package) += binary-headers
endif
binary-arch: $(binary-arch-deps-true)
