package=qrencode

$(package)_version=3.4.3

# We already provide the file locally in depends/sources
$(package)_download_path=

# Correct filename pattern
$(package)_file_name=qrencode-$($(package)_version).tar.bz2

# Correct checksum for your downloaded file
$(package)_sha256_hash=dfd71487513c871bad485806bfd1fdb304dedc84d2b01a8fb8e0940b50597a98

define $(package)_set_vars
$(package)_config_opts=--disable-shared --without-tools --disable-sdltest
$(package)_config_opts_linux=--with-pic
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
