package=native_protobuf
# 3.21.12 is the last protobuf release that still ships an autotools build and
# needs no abseil. It carries the fixes for CVE-2021-22569, CVE-2022-1941 and
# CVE-2022-3171 that 2.5.0 lacks. Ubuntu 22.04 (the Linux release build) uses
# 3.12.4, so the generated paymentrequest.pb.* code is known to build on 3.x.
$(package)_version=3.21.12
$(package)_download_path=https://github.com/protocolbuffers/protobuf/releases/download/v21.12
$(package)_file_name=protobuf-cpp-$($(package)_version).tar.gz
$(package)_sha256_hash=4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460

define $(package)_set_vars
$(package)_config_opts=--disable-shared --without-zlib
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -C src protoc
endef

define $(package)_stage_cmds
  $(MAKE) -C src DESTDIR=$($(package)_staging_dir) install-strip
endef

define $(package)_postprocess_cmds
  rm -rf lib include
endef
