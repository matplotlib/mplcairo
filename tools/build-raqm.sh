#!/bin/bash
set -e

ver=0.4.0
toplevel="$(git rev-parse --show-toplevel)"
mkdir -p "$toplevel/build"
cd "$toplevel/build"
rm -rf "raqm-$ver"
rm -rf "raqm-prefix"

if ! [[ -f "raqm-$ver.tar.gz" ]]; then
    curl -L "https://github.com/HOST-Oman/libraqm/releases/download/v$ver/raqm-$ver.tar.gz" \
	> "raqm-$ver.tar.gz"
fi

tar -xvzf "raqm-$ver.tar.gz"
cd "raqm-$ver"
patch -p1 <<'EOF'
diff --git a/Makefile.am b/Makefile.am
index bfd0f52..3876f50 100644
--- a/Makefile.am
+++ b/Makefile.am
@@ -2,7 +2,7 @@ NULL =
 
 ACLOCAL_AMFLAGS = -I m4
 
-SUBDIRS = src docs tests
+SUBDIRS = src tests
 
 pkgconfigdir = $(libdir)/pkgconfig
 pkgconfig_DATA = @PACKAGE@.pc
diff --git a/configure.ac b/configure.ac
index e28e57f..02dfd1d 100644
--- a/configure.ac
+++ b/configure.ac
@@ -45,8 +45,6 @@ CFLAGS="$_save_cflags"
 
 AM_CONDITIONAL(HAVE_GLIB, $have_glib)
 
-GTK_DOC_CHECK([1.14],[--flavour no-tmpl])
-
 case $build_os in
     mingw*)
         AX_APPEND_FLAG([-D__USE_MINGW_ANSI_STDIO=1])
@@ -58,8 +56,6 @@ AC_CONFIG_FILES([
     raqm.pc
     src/Makefile
     tests/Makefile
-    docs/Makefile
-    docs/version.xml
 ])
 
 AC_OUTPUT
EOF
autoreconf -fiv
./configure --prefix="$toplevel/build/raqm-prefix"
make CFLAGS=-fPIC
make install
