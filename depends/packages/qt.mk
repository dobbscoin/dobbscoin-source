package=qt
# Qt 5.15 is the last 5.x LTS. 5.15.19 is the newest release whose opensource
# sources are published (download.qt.io/archive). Recipe and patches follow
# Bitcoin Core's depends/packages/qt.mk (v25-v28, Qt 5.15.x, mingw gcc 12),
# adapted to this older depends framework and to what this GUI still uses:
#   - OpenSSL stays linked and QtNetwork keeps http/networkproxy/socks5,
#     because the BIP70 payment server (paymentserver.cpp) fetches payment
#     requests over HTTPS and inspects QSslCertificate chains.
#   - Qt itself is built as C++17 (5.15.19 uses std::size in qlocale_win.cpp);
#     the headers remain usable from this C++14 tree.
#   - zlib comes from the zlib package (-system-zlib) rather than Qt's bundled
#     copy, so it gets zlib's own security updates.
# Core patches dropped because 5.15.19 already contains them upstream:
#   fix-macos-linker, clang_18_libpng, zlib-timebits64.
# memory_resource.patch is macOS-only and does not apply to 5.15.19.
$(package)_version=5.15.19
$(package)_download_path=https://download.qt.io/archive/qt/5.15/$($(package)_version)/submodules
$(package)_suffix=everywhere-opensource-src-$($(package)_version).tar.xz
$(package)_file_name=qtbase-$($(package)_suffix)
$(package)_sha256_hash=51e91c73abacab81e64efd01bf95794e79c0e605ad80947a024769f0dd620a32
$(package)_dependencies=openssl zlib
$(package)_linux_dependencies=freetype fontconfig libxcb libX11 xproto libXext
$(package)_qt_libs=corelib network widgets gui plugins testlib
$(package)_linguist_tools = lrelease lupdate lconvert
$(package)_patches = qt.pro
$(package)_patches += qttools_src.pro
$(package)_patches += mac-qmake.conf
$(package)_patches += fix_qt_pkgconfig.patch
$(package)_patches += no-xlib.patch
$(package)_patches += dont_hardcode_pwd.patch
$(package)_patches += qtbase-moc-ignore-gcc-macro.patch
$(package)_patches += no_warnings_for_symbols.patch
$(package)_patches += rcc_hardcode_timestamp.patch
$(package)_patches += duplicate_lcqpafonts.patch
$(package)_patches += guix_cross_lib_path.patch
$(package)_patches += utc_from_string_no_optimize.patch
$(package)_patches += windows_lto.patch
$(package)_patches += darwin_no_libm.patch

$(package)_qttranslations_file_name=qttranslations-$($(package)_suffix)
$(package)_qttranslations_sha256_hash=967e0d259af13f47e853f183cffc9671cc054380188fee28402f6425132a2c7b

$(package)_qttools_file_name=qttools-$($(package)_suffix)
$(package)_qttools_sha256_hash=2694af886130d63b8a42bcd138f4035fd8108273ebd16781f87300da352f1aa6

$(package)_extra_sources  = $($(package)_qttranslations_file_name)
$(package)_extra_sources += $($(package)_qttools_file_name)

define $(package)_set_vars
$(package)_config_env = QT_MAC_SDK_NO_VERSION_CHECK=1
$(package)_config_opts_release = -release
$(package)_config_opts_release += -silent
$(package)_config_opts_debug = -debug
$(package)_config_opts_debug += -optimized-tools
$(package)_config_opts += -bindir $(build_prefix)/bin
$(package)_config_opts += -c++std c++17
$(package)_config_opts += -confirm-license
$(package)_config_opts += -hostprefix $(build_prefix)
$(package)_config_opts += -no-compile-examples
$(package)_config_opts += -no-cups
$(package)_config_opts += -no-egl
$(package)_config_opts += -no-eglfs
$(package)_config_opts += -no-evdev
$(package)_config_opts += -no-gif
$(package)_config_opts += -no-glib
$(package)_config_opts += -no-icu
$(package)_config_opts += -no-ico
$(package)_config_opts += -no-iconv
$(package)_config_opts += -no-kms
$(package)_config_opts += -no-linuxfb
$(package)_config_opts += -no-libjpeg
$(package)_config_opts += -no-libproxy
$(package)_config_opts += -no-libudev
$(package)_config_opts += -no-mimetype-database
$(package)_config_opts += -no-mtdev
$(package)_config_opts += -openssl-linked
$(package)_config_opts += -no-openvg
$(package)_config_opts += -no-reduce-relocations
$(package)_config_opts += -no-schannel
$(package)_config_opts += -no-sctp
$(package)_config_opts += -no-securetransport
$(package)_config_opts += -no-sql-db2
$(package)_config_opts += -no-sql-ibase
$(package)_config_opts += -no-sql-oci
$(package)_config_opts += -no-sql-tds
$(package)_config_opts += -no-sql-mysql
$(package)_config_opts += -no-sql-odbc
$(package)_config_opts += -no-sql-psql
$(package)_config_opts += -no-sql-sqlite
$(package)_config_opts += -no-sql-sqlite2
$(package)_config_opts += -no-system-proxies
$(package)_config_opts += -no-use-gold-linker
$(package)_config_opts += -no-zstd
$(package)_config_opts += -nomake examples
$(package)_config_opts += -nomake tests
$(package)_config_opts += -nomake tools
$(package)_config_opts += -opensource
$(package)_config_opts += -pkg-config
$(package)_config_opts += -prefix $(host_prefix)
$(package)_config_opts += -qt-libpng
$(package)_config_opts += -qt-pcre
$(package)_config_opts += -qt-harfbuzz
$(package)_config_opts += -system-zlib
$(package)_config_opts += -static
$(package)_config_opts += -v
$(package)_config_opts += -no-feature-bearermanagement
$(package)_config_opts += -no-feature-colordialog
$(package)_config_opts += -no-feature-commandlineparser
$(package)_config_opts += -no-feature-concurrent
$(package)_config_opts += -no-feature-dial
$(package)_config_opts += -no-feature-fontcombobox
$(package)_config_opts += -no-feature-ftp
$(package)_config_opts += -no-feature-image_heuristic_mask
$(package)_config_opts += -no-feature-keysequenceedit
$(package)_config_opts += -no-feature-lcdnumber
$(package)_config_opts += -no-feature-networkdiskcache
$(package)_config_opts += -no-feature-pdf
$(package)_config_opts += -no-feature-printdialog
$(package)_config_opts += -no-feature-printer
$(package)_config_opts += -no-feature-printpreviewdialog
$(package)_config_opts += -no-feature-printpreviewwidget
$(package)_config_opts += -no-feature-sessionmanager
$(package)_config_opts += -no-feature-sql
$(package)_config_opts += -no-feature-sqlmodel
$(package)_config_opts += -no-feature-statemachine
$(package)_config_opts += -no-feature-syntaxhighlighter
$(package)_config_opts += -no-feature-textbrowser
$(package)_config_opts += -no-feature-textmarkdownwriter
$(package)_config_opts += -no-feature-textodfwriter
$(package)_config_opts += -no-feature-topleveldomain
$(package)_config_opts += -no-feature-udpsocket
$(package)_config_opts += -no-feature-undocommand
$(package)_config_opts += -no-feature-undogroup
$(package)_config_opts += -no-feature-undostack
$(package)_config_opts += -no-feature-undoview
$(package)_config_opts += -no-feature-vnc
$(package)_config_opts += -no-feature-wizard
$(package)_config_opts += -no-feature-xml

$(package)_config_opts_darwin = -no-dbus
$(package)_config_opts_darwin += -no-opengl
$(package)_config_opts_darwin += -pch
$(package)_config_opts_darwin += -no-feature-corewlan
$(package)_config_opts_darwin += -no-freetype
$(package)_config_opts_darwin += QMAKE_MACOSX_DEPLOYMENT_TARGET=$(OSX_MIN_VERSION)

ifneq ($(build_os),darwin)
$(package)_config_opts_darwin += -xplatform macx-clang-linux
$(package)_config_opts_darwin += -device-option MAC_SDK_PATH=$(OSX_SDK)
$(package)_config_opts_darwin += -device-option MAC_SDK_VERSION=$(OSX_SDK_VERSION)
$(package)_config_opts_darwin += -device-option CROSS_COMPILE="$(host)-"
$(package)_config_opts_darwin += -device-option MAC_TARGET=$(host)
endif

# The static-Qt Linux path (USE_LINUX_STATIC_QT5) is not exercised by any
# release build -- Linux releases link the distribution's Qt 5.15.
$(package)_config_opts_linux = -xcb
$(package)_config_opts_linux += -no-xcb-xlib
$(package)_config_opts_linux += -no-feature-xlib
$(package)_config_opts_linux += -system-freetype
$(package)_config_opts_linux += -fontconfig
$(package)_config_opts_linux += -no-opengl
$(package)_config_opts_linux += -no-feature-vulkan
$(package)_config_opts_linux += -dbus-runtime
$(package)_config_opts_arm_linux  = -platform linux-g++ -xplatform $(host)
$(package)_config_opts_i686_linux  = -xplatform linux-g++-32

$(package)_config_opts_mingw32 = -no-opengl
$(package)_config_opts_mingw32 += -no-dbus
$(package)_config_opts_mingw32 += -no-freetype
$(package)_config_opts_mingw32 += -xplatform win32-g++
$(package)_config_opts_mingw32 += "QMAKE_CFLAGS = '$($(package)_cflags) $($(package)_cppflags)'"
$(package)_config_opts_mingw32 += "QMAKE_CXX = '$($(package)_cxx)'"
$(package)_config_opts_mingw32 += "QMAKE_CXXFLAGS = '$($(package)_cxxflags) $($(package)_cppflags)'"
$(package)_config_opts_mingw32 += "QMAKE_LINK = '$($(package)_cxx)'"
$(package)_config_opts_mingw32 += "QMAKE_LFLAGS = '$($(package)_ldflags)'"
$(package)_config_opts_mingw32 += "QMAKE_LIB = '$($(package)_ar) rc'"
$(package)_config_opts_mingw32 += -device-option CROSS_COMPILE="$(host)-"
$(package)_config_opts_mingw32 += -pch
# A static libcrypto on Windows needs these system libraries after it.
$(package)_config_opts_mingw32 += OPENSSL_LIBS="-lssl -lcrypto -lws2_32 -lgdi32 -lcrypt32 -luser32 -ladvapi32"
endef

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qttranslations_file_name),$($(package)_qttranslations_file_name),$($(package)_qttranslations_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qttools_file_name),$($(package)_qttools_file_name),$($(package)_qttools_sha256_hash))
endef

define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source)" > $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qttranslations_sha256_hash)  $($(package)_source_dir)/$($(package)_qttranslations_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qttools_sha256_hash)  $($(package)_source_dir)/$($(package)_qttools_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  mkdir qtbase && \
  tar --no-same-owner --strip-components=1 -xf $($(package)_source) -C qtbase && \
  mkdir qttranslations && \
  tar --no-same-owner --strip-components=1 -xf $($(package)_source_dir)/$($(package)_qttranslations_file_name) -C qttranslations && \
  mkdir qttools && \
  tar --no-same-owner --strip-components=1 -xf $($(package)_source_dir)/$($(package)_qttools_file_name) -C qttools
endef

# 1. Apply the patches.
# 2. Create a macOS-Clang-Linux mkspec from mac-qmake.conf.
# 3. Put our C, CXX and LD flags into gcc-base.conf (non-host builds only).
define $(package)_preprocess_cmds
  cp $($(package)_patch_dir)/qt.pro qt.pro && \
  cp $($(package)_patch_dir)/qttools_src.pro qttools/src/src.pro && \
  patch -p1 -i $($(package)_patch_dir)/dont_hardcode_pwd.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix_qt_pkgconfig.patch && \
  patch -p1 -i $($(package)_patch_dir)/no-xlib.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase-moc-ignore-gcc-macro.patch && \
  patch -p1 -i $($(package)_patch_dir)/no_warnings_for_symbols.patch && \
  patch -p1 -i $($(package)_patch_dir)/rcc_hardcode_timestamp.patch && \
  patch -p1 -i $($(package)_patch_dir)/duplicate_lcqpafonts.patch && \
  patch -p1 -i $($(package)_patch_dir)/utc_from_string_no_optimize.patch && \
  patch -p1 -i $($(package)_patch_dir)/guix_cross_lib_path.patch && \
  patch -p1 -i $($(package)_patch_dir)/windows_lto.patch && \
  patch -p1 -i $($(package)_patch_dir)/darwin_no_libm.patch && \
  mkdir -p qtbase/mkspecs/macx-clang-linux &&\
  cp -f qtbase/mkspecs/macx-clang/qplatformdefs.h qtbase/mkspecs/macx-clang-linux/ &&\
  cp -f $($(package)_patch_dir)/mac-qmake.conf qtbase/mkspecs/macx-clang-linux/qmake.conf && \
  echo "!host_build: QMAKE_CFLAGS     += $($(package)_cflags) $($(package)_cppflags)" >> qtbase/mkspecs/common/gcc-base.conf && \
  echo "!host_build: QMAKE_CXXFLAGS   += $($(package)_cxxflags) $($(package)_cppflags)" >> qtbase/mkspecs/common/gcc-base.conf && \
  echo "!host_build: QMAKE_LFLAGS     += $($(package)_ldflags)" >> qtbase/mkspecs/common/gcc-base.conf
endef

define $(package)_config_cmds
  export PKG_CONFIG_SYSROOT_DIR=/ && \
  export PKG_CONFIG_LIBDIR=$(host_prefix)/lib/pkgconfig && \
  export PKG_CONFIG_PATH=$(host_prefix)/share/pkgconfig && \
  cd qtbase && \
  ./configure -top-level $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) -C qtbase/src INSTALL_ROOT=$($(package)_staging_dir) $(addsuffix -install_subtargets,$(addprefix sub-,$($(package)_qt_libs))) && \
  $(MAKE) -C qttools/src/linguist INSTALL_ROOT=$($(package)_staging_dir) $(addsuffix -install_subtargets,$(addprefix sub-,$($(package)_linguist_tools))) && \
  $(MAKE) -C qttranslations INSTALL_ROOT=$($(package)_staging_dir) install_subtargets
endef

define $(package)_postprocess_cmds
  rm -rf native/mkspecs/ native/lib/ lib/cmake/ && \
  rm -f lib/lib*.la
endef
