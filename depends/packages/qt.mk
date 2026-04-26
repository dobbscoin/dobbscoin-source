PACKAGE=qt
$(package)_version=5.2.1
$(package)_download_path=https://download.qt.io/new_archive/qt/5.2/$($(package)_version)/single
$(package)_file_name=$(package)-everywhere-opensource-src-$($(package)_version).tar.gz
$(package)_sha256_hash=84e924181d4ad6db00239d87250cc89868484a14841f77fb85ab1f1dbdcd7da1
$(package)_dependencies=openssl
$(package)_linux_dependencies=freetype fontconfig dbus libxcb libX11 xproto libXext
$(package)_build_subdir=qtbase
$(package)_qt_libs=corelib network widgets gui plugins testlib
$(package)_patches=mac-qmake.conf fix-xcb-include-order.patch qt5-tablet-osx.patch fix-qmake-bootstrap-feature-paths.patch fix-configure-configtests-cache.patch fix-configure-windows-prf.patch fix-bootstrap-include-paths.patch fix-bootstrap-tool-include-paths.patch fix-corelib-include-paths.patch fix-moc-tool-command-generation.patch fix-rcc-tool-command-generation.patch fix-uic-tool-command-generation.patch fix-extra-compiler-command-expansion.patch fix-bootstrap-moc-arg-parsing.patch fix-exclusive-builds-recursion.patch fix-module-pri-child-include-paths.patch fix-module-include-propagation.patch fix-qtaddmodule-private-includes.patch fix-win32-module-include-export.patch fix-moc-forwarded-interface-lookup.patch fix-qnetworkinterface-win-header-deps.patch fix-win32-moc-platform-defines.patch fix-qlocalsocket-moc-windows-branch.patch fix-qwineventnotifier-moc-windows-guard.patch fix-qsslsocket-openssl-moc-windows-guard.patch fix-lrelease-link-xml.patch

define $(package)_set_vars
$(package)_config_opts_release = -release
$(package)_config_opts_debug   = -debug
$(package)_config_opts += -opensource -confirm-license -no-audio-backend -no-sql-tds -no-glib -no-icu -no-warnings-are-errors
$(package)_config_opts += -no-cups -no-iconv -no-gif -no-audio-backend -no-freetype
$(package)_config_opts += -no-dbus -DQT_NO_DBUS
$(package)_config_opts += -no-sql-sqlite -no-nis -no-cups -no-iconv -no-pch
$(package)_config_opts += -no-gif -no-feature-style-plastique
$(package)_config_opts += -no-qml-debug -no-pch -no-nis -nomake examples -nomake tests
$(package)_config_opts += -no-feature-style-cde -no-feature-style-s60 -no-feature-style-motif
$(package)_config_opts += -no-feature-style-windowsmobile -no-feature-style-windowsce
$(package)_config_opts += -no-feature-style-cleanlooks
$(package)_config_opts += -no-sql-db2 -no-sql-ibase -no-sql-oci -no-sql-tds -no-sql-mysql
$(package)_config_opts += -no-sql-odbc -no-sql-psql -no-sql-sqlite -no-sql-sqlite2
$(package)_config_opts += -skip qtsvg -skip qtwebkit -skip qtwebkit-examples -skip qtserialport
$(package)_config_opts += -skip qtdeclarative -skip qtmultimedia -skip qtimageformats -skip qtx11extras
$(package)_config_opts += -skip qtlocation -skip qtsensors -skip qtquick1 -skip qtxmlpatterns
$(package)_config_opts += -skip qtquickcontrols -skip qtactiveqt -skip qtconnectivity -skip qtmacextras
$(package)_config_opts += -skip qtwinextras -skip qtxmlpatterns -skip qtscript -skip qtdoc

$(package)_config_opts += -prefix $(host_prefix) -bindir $(build_prefix)/bin
$(package)_config_opts += -no-c++11 -openssl-linked  -v -shared -no-static -silent -pkg-config
$(package)_config_opts += -no-accessibility -no-feature-accessibility
$(package)_config_opts += -qt-libpng -qt-libjpeg -qt-zlib -qt-pcre

ifneq ($(build_os),darwin)
$(package)_config_opts_darwin = -xplatform macx-clang-linux -device-option MAC_SDK_PATH=$(OSX_SDK) -device-option CROSS_COMPILE="$(host)-"
$(package)_config_opts_darwin += -device-option MAC_MIN_VERSION=$(OSX_MIN_VERSION) -device-option MAC_TARGET=$(host)
endif

$(package)_config_opts_linux  = -qt-xkbcommon -qt-xcb  -no-eglfs -no-linuxfb -system-freetype -no-sm -fontconfig -no-xinput2 -no-libudev -no-egl -no-opengl
$(package)_config_opts_arm_linux  = -platform linux-g++ -xplatform $(host)
$(package)_config_opts_i686_linux  = -xplatform linux-g++-32
$(package)_config_opts_mingw32  = -no-opengl -xplatform win32-g++ -device-option CROSS_COMPILE="$(host)-"
$(package)_build_env  = QT_RCC_TEST=1
endef

define $(package)_preprocess_cmds
  sed -i.old "s|updateqm.commands = \$$$$\$$$$LRELEASE|updateqm.commands = $($(package)_extract_dir)/qttools/bin/lrelease|" qttranslations/translations/translations.pro && \
  sed -i.old "s/src_plugins.depends = src_sql src_xml src_network/src_plugins.depends = src_xml src_network/" qtbase/src/src.pro && \
  sed -i.old "s/SUBDIRS += src_plugins src_tools_qdoc/SUBDIRS += src_plugins/" qtbase/src/src.pro && \
  sed -i.old "/XIproto.h/d" qtbase/src/plugins/platforms/xcb/qxcbxsettings.cpp && \
  sed -i.old 's/if \[ "$$$$XPLATFORM_MAC" = "yes" \]; then xspecvals=$$$$(macSDKify/if \[ "$$$$BUILD_ON_MAC" = "yes" \]; then xspecvals=$$$$(macSDKify/' qtbase/configure && \
  mkdir -p qtbase/mkspecs/macx-clang-linux &&\
  mkdir -p qtbase/mkspecs/modules && \
  printf 'CONFIG += release shared cross_compile qpa\nQT_CONFIG += minimal\n' > qtbase/mkspecs/qconfig.pri && \
  printf 'QT_BUILD_PARTS += libs tools\nQT_QCONFIG_PATH = qconfig.h\nQT_COORD_TYPE += double\n' > qtbase/mkspecs/qmodule.pri && \
  printf 'QT.core.VERSION = 5.2.1\nQT.core.name = QtCore\nQT.core.module = Qt5Core\nQT.core.depends =\nQT.core.module_config = v2 no_link\nQT_MODULES += core\n' > qtbase/mkspecs/modules/qt_lib_core.pri && \
  cp -f qtbase/mkspecs/macx-clang/Info.plist.lib qtbase/mkspecs/macx-clang-linux/ &&\
  cp -f qtbase/mkspecs/macx-clang/Info.plist.app qtbase/mkspecs/macx-clang-linux/ &&\
  cp -f qtbase/mkspecs/macx-clang/qplatformdefs.h qtbase/mkspecs/macx-clang-linux/ &&\
  cp -f $($(package)_patch_dir)/mac-qmake.conf qtbase/mkspecs/macx-clang-linux/qmake.conf && \
  patch -p1 < $($(package)_patch_dir)/fix-xcb-include-order.patch && \
  patch -p1 < $($(package)_patch_dir)/qt5-tablet-osx.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-qmake-bootstrap-feature-paths.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-configure-configtests-cache.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-configure-windows-prf.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-bootstrap-include-paths.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-bootstrap-tool-include-paths.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-corelib-include-paths.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-moc-tool-command-generation.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-rcc-tool-command-generation.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-uic-tool-command-generation.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-extra-compiler-command-expansion.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-bootstrap-moc-arg-parsing.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-exclusive-builds-recursion.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-module-pri-child-include-paths.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-module-include-propagation.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-qtaddmodule-private-includes.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-win32-module-include-export.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-moc-forwarded-interface-lookup.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-qnetworkinterface-win-header-deps.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-win32-moc-platform-defines.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-qlocalsocket-moc-windows-branch.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-qwineventnotifier-moc-windows-guard.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-qsslsocket-openssl-moc-windows-guard.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-lrelease-link-xml.patch && \
  ln -snf ../qtbase qttools/qtbase && \
  ln -snf ../qtbase qttranslations/qtbase && \
  perl -0pi -e 's/foreach \(const QXmlStreamAttribute &attribute, reader\.attributes\(\)\) \{/const QXmlStreamAttributes attributes = reader.attributes();\n    for (int i = 0; i < attributes.size(); ++i) {\n        const QXmlStreamAttribute attribute = attributes.at(i);\n/g; s/const QXmlStreamAttributes attributes = reader\.attributes\(\);\n    foreach \(const QXmlStreamAttribute &attribute, attributes\) \{/const QXmlStreamAttributes attributes = reader.attributes();\n    for (int i = 0; i < attributes.size(); ++i) {\n        const QXmlStreamAttribute attribute = attributes.at(i);\n/g' qtbase/src/tools/uic/ui4.cpp && \
  echo "QMAKE_CFLAGS     += $($(package)_cflags) $($(package)_cppflags)" >> qtbase/mkspecs/common/gcc-base.conf && \
  echo "QMAKE_CXXFLAGS   += $($(package)_cxxflags) $($(package)_cppflags)" >> qtbase/mkspecs/common/gcc-base.conf && \
  echo "QMAKE_LFLAGS     += $($(package)_ldflags)" >> qtbase/mkspecs/common/gcc-base.conf && \
  echo "QMAKE_LIBDIR     += $(host_prefix)/lib" >> qtbase/mkspecs/common/gcc-base.conf && \
  sed -i.old "s/CONFIG                 += debug_and_release debug_and_release_target precompile_header/CONFIG                 += debug_and_release debug_and_release_target/" qtbase/mkspecs/win32-g++/qmake.conf && \
  sed -i.old "s|QMAKE_CFLAGS            = |QMAKE_CFLAGS            = $($(package)_cflags) $($(package)_cppflags) |" qtbase/mkspecs/win32-g++/qmake.conf && \
  sed -i.old "s|QMAKE_LFLAGS            = |QMAKE_LFLAGS            = $($(package)_ldflags) |" qtbase/mkspecs/win32-g++/qmake.conf && \
  sed -i.old "s|QMAKE_CXXFLAGS          = |QMAKE_CXXFLAGS            = $($(package)_cxxflags) $($(package)_cppflags) |" qtbase/mkspecs/win32-g++/qmake.conf && \
  echo "QMAKE_LIBDIR     += $(host_prefix)/lib" >> qtbase/mkspecs/win32-g++/qmake.conf && \
  echo "LIBS += -L$(host_prefix)/lib -lssl -lcrypto -lws2_32 -lgdi32 -lcrypt32" >> qtbase/mkspecs/win32-g++/qmake.conf
endef

define $(package)_config_cmds
  export PKG_CONFIG_SYSROOT_DIR=/ && \
  export PKG_CONFIG_LIBDIR=$(host_prefix)/lib/pkgconfig && \
  export PKG_CONFIG_PATH=$(host_prefix)/share/pkgconfig  && \
  export CPATH=$(host_prefix)/include && \
  export LDFLAGS="$($(package)_ldflags) -L$(host_prefix)/lib" && \
  ./configure $($(package)_config_opts) && \
  $(MAKE) sub-src-qmake_all
endef

define $(package)_build_cmds
  export CPATH=$(host_prefix)/include && \
  $(MAKE) -C src $(addprefix sub-,$($(package)_qt_libs)) && \
  (cd src/plugins/platforms/windows && \
   ../../../../bin/qmake ../../../../src/plugins/platforms/windows/windows.pro -o Makefile && \
   $(MAKE)) && \
  cd ../qttools/src/linguist/lrelease && \
  rm -f .qmake.cache && \
  cp -f ../../../.qmake.conf .qmake.conf && \
  QMAKEFEATURES=$$PWD/../../../../qtbase/mkspecs/features QMAKEPATH=$$PWD/../../../../qtbase ../../../../qtbase/bin/qmake -spec $$PWD/../../../../qtbase/mkspecs/win32-g++ lrelease.pro -o Makefile && \
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) INSTALL_ROOT=$($(package)_staging_dir) install && cd .. && \
  $(MAKE) -C qtbase/src/plugins/platforms/windows INSTALL_ROOT=$($(package)_staging_dir) install && \
  $(MAKE) -C qttools/src/linguist/lrelease INSTALL_ROOT=$($(package)_staging_dir) install && \
  mkdir -p $($(package)_staging_prefix_dir)/bin $($(package)_staging_prefix_dir)/plugins/platforms && \
  if test -d qtbase/lib; then cp -a qtbase/lib/Qt5*.dll $($(package)_staging_prefix_dir)/bin/ 2>/dev/null || true; fi && \
  if test -d qtbase/plugins/platforms; then cp -a qtbase/plugins/platforms/*.dll $($(package)_staging_prefix_dir)/plugins/platforms/ 2>/dev/null || true; fi && \
  if test -d qtbase/src/plugins/platforms/windows; then find qtbase/src/plugins/platforms/windows -name 'qwindows.dll' -exec cp -a {} $($(package)_staging_prefix_dir)/plugins/platforms/ \; ; fi && \
  cp -a qtbase/plugins/platforms/. $($(package)_staging_prefix_dir)/plugins/platforms/ && \
  cp -a qtbase/include/. $($(package)_staging_prefix_dir)/include/ && \
  cp -a qtbase/src/. $($(package)_staging_prefix_dir)/src/ && \
  cp -a qtbase/include/QtCore/qplugin.h $($(package)_staging_prefix_dir)/include/qplugin.h && \
  cp -a qtbase/include/QtCore/QtPlugin $($(package)_staging_prefix_dir)/include/QtPlugin && \
  if `test -f qtbase/src/plugins/platforms/xcb/xcb-static/libxcb-static.a`; then \
    cp qtbase/src/plugins/platforms/xcb/xcb-static/libxcb-static.a $($(package)_staging_prefix_dir)/lib; \
  fi
endef

define $(package)_postprocess_cmds
  rm -rf mkspecs/ lib/cmake/ && \
  rm lib/libQt5Bootstrap.a lib/lib*.la lib/*.prl plugins/*/*.prl
endef
