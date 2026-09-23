package=openssl
# OpenSSL 3.5 is the current LTS branch (supported to 2030-04-08). 3.0 LTS
# reached end of life on 2026-09-07. Tarball and its published .sha256 come from
# the GitHub release; https://www.openssl.org/source/ serves the same file.
$(package)_version=3.5.8
$(package)_download_path=https://github.com/openssl/openssl/releases/download/openssl-$($(package)_version)
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2

define $(package)_set_vars
$(package)_config_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CC="$($(package)_cc)"
$(package)_config_env_mingw32=RC="$(host)-windres"
$(package)_config_opts=--prefix=$(host_prefix) --openssldir=$(host_prefix)/etc/openssl --libdir=lib
$(package)_config_opts+=no-shared no-module no-dso no-zlib no-tests no-docs no-apps no-engine no-legacy
$(package)_config_opts+=no-camellia no-cast no-idea no-md2 no-mdc2 no-rc5 no-seed no-whirlpool no-gost
$(package)_config_opts+=no-sctp no-srp no-ssl3 no-weak-ssl-ciphers no-quic no-comp no-afalgeng no-uplink
$(package)_config_opts+=$($(package)_cflags) $($(package)_cppflags)
$(package)_config_opts_linux=-fPIC
$(package)_config_opts_x86_64_linux=linux-x86_64
$(package)_config_opts_i686_linux=linux-generic32
$(package)_config_opts_arm_linux=linux-generic32
$(package)_config_opts_aarch64_linux=linux-aarch64
$(package)_config_opts_x86_64_darwin=darwin64-x86_64-cc
$(package)_config_opts_x86_64_mingw32=mingw64
$(package)_config_opts_i686_mingw32=mingw
endef

define $(package)_config_cmds
  ./Configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE) build_libs
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_dev
endef

define $(package)_postprocess_cmds
  rm -rf share bin etc lib/cmake
endef
