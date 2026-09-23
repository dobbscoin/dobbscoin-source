package=boost
# 1.83.0 is the newest Boost this tree compiles against unchanged, and what
# Debian 13 and Ubuntu 24.04 ship. (The Linux release currently builds against
# Ubuntu 22.04's 1.74.) 1.86 does not compile: Boost.Filesystem dropped the
# deprecated v3 calls the tree still uses (path::is_complete, basename,
# extension), and 1.87 also drops asio::io_service. Going further needs
# source changes.
$(package)_version=1_83_0
$(package)_download_path=https://archives.boost.io/release/1.83.0/source
$(package)_file_name=$(package)_$($(package)_version).tar.bz2
$(package)_sha256_hash=6478edfe2f3305127cffe8caf73ea0176c53769f4bf1585be237eb30798c3b8e

define $(package)_set_vars
$(package)_config_opts_release=variant=release
$(package)_config_opts_debug=variant=debug
$(package)_config_opts=--layout=tagged --build-type=complete --user-config=user-config.jam
$(package)_config_opts+=threading=multi link=static -sNO_COMPRESSION=1
$(package)_config_opts_linux=target-os=linux threadapi=pthread runtime-link=shared
$(package)_config_opts_darwin=target-os=darwin runtime-link=shared
$(package)_config_opts_mingw32=target-os=windows binary-format=pe threadapi=win32 runtime-link=static
$(package)_config_opts_x86_64=architecture=x86 address-model=64
$(package)_config_opts_i686=architecture=x86 address-model=32
$(package)_config_opts_aarch64=address-model=64
$(package)_config_opts_armv7a=address-model=32
$(package)_toolset_$(host_os)=gcc
$(package)_archiver_$(host_os)=$($(package)_ar)
$(package)_toolset_darwin=darwin
$(package)_archiver_darwin=$($(package)_libtool)
$(package)_config_libraries=chrono,filesystem,program_options,system,thread,test
$(package)_cxxflags=-std=c++14 -fvisibility=hidden
$(package)_cxxflags_linux=-fPIC
endef

define $(package)_preprocess_cmds
  echo "using $($(package)_toolset_$(host_os)) : : $($(package)_cxx) : <cxxflags>\"$($(package)_cxxflags) $($(package)_cppflags)\" <linkflags>\"$($(package)_ldflags)\" <archiver>\"$($(package)_archiver_$(host_os))\" <striper>\"$(host_STRIP)\"  <ranlib>\"$(host_RANLIB)\" <rc>\"$(host_WINDRES)\" : ;" > user-config.jam
endef

define $(package)_config_cmds
  ./bootstrap.sh --without-icu --with-libraries=$($(package)_config_libraries) --with-toolset=gcc
endef

define $(package)_build_cmds
  ./b2 -d2 -j2 -d1 --prefix=$($(package)_staging_prefix_dir) $($(package)_config_opts) stage
endef

define $(package)_stage_cmds
  ./b2 -d0 -j4 --prefix=$($(package)_staging_prefix_dir) $($(package)_config_opts) install
endef
