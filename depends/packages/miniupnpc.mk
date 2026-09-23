package=miniupnpc
# 2.x is API version 21. src/net.cpp already selects the matching
# upnpDiscover()/UPNP_GetValidIGD() signatures on MINIUPNPC_API_VERSION.
$(package)_version=2.3.3
$(package)_download_path=https://miniupnp.tuxfamily.org/files
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=d52a0afa614ad6c088cc9ddff1ae7d29c8c595ac5fdd321170a05f41e634bd1a

define $(package)_set_vars
$(package)_build_opts=CC="$($(package)_cc)"
$(package)_build_opts_darwin=LIBTOOL="$($(package)_libtool)"
# Makefile.mingw appends -D_WIN32_WINNT=0x501 (XP) to CPPFLAGS; overriding
# CPPFLAGS on the command line replaces that with Windows 7 (0x0601), which is
# what the rest of the build targets. The library objects themselves are
# compiled with -DMINIUPNP_STATICLIB by Makefile.mingw's %.o rule.
$(package)_build_opts_mingw32=-f Makefile.mingw CPPFLAGS="-DNDEBUG -D_WIN32_WINNT=0x0601 -Iinclude -I. $($(package)_cppflags)"
$(package)_build_env+=CFLAGS="$($(package)_cflags) $($(package)_cppflags)" AR="$($(package)_ar)"
endef

define $(package)_build_cmds
	$(MAKE) $(if $(findstring mingw32,$(host_os)),libminiupnpc.a,build/libminiupnpc.a) $($(package)_build_opts)
endef

define $(package)_stage_cmds
	mkdir -p $($(package)_staging_prefix_dir)/include/miniupnpc $($(package)_staging_prefix_dir)/lib &&\
	install include/*.h $($(package)_staging_prefix_dir)/include/miniupnpc &&\
	install `test -f build/libminiupnpc.a && echo build/libminiupnpc.a || echo libminiupnpc.a` $($(package)_staging_prefix_dir)/lib
endef
