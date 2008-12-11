# Packages.make
#
# Copyright (C) $year Texas Instruments Incorporated - http://www.ti.com/
#
# This program is free software; you can redistribute it and/or modify 
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation version 2.1 of the License.
#
# This program is distributed #as is# WITHOUT ANY WARRANTY of any kind,
# whether express or implied; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

#------------------------------------------------------------------------------
# Packages processed by this build script.
#------------------------------------------------------------------------------

#------------------------------------------------------------------------------
# Package glib
#------------------------------------------------------------------------------
PACKAGE_glib_BUILD_TARGET       = glib
PACKAGE_glib_ARCHIVE_BASENAME   = glib-2.18.1
PACKAGE_glib_PRECONFIG_PATCHES  =
PACKAGE_glib_CONFIGURE_OPTS     = ac_cv_lib_rt_clock_gettime=no glib_cv_stack_grows=no glib_cv_monotonic_clock=yes glib_cv_uscore=no ac_cv_func_posix_getpwuid_r=yes ac_cv_func_posix_getgrgid_r=yes
PACKAGE_glib_POSTCONFIG_PATCHES = glib1_2_18_1
PACKAGE_glib_BUILD_DIRS         =
PACKAGE_glib_DESCRIPTION        = GLib library
BASE_PACKAGES += $(PACKAGE_glib_BUILD_TARGET)

#------------------------------------------------------------------------------
# Package check
#------------------------------------------------------------------------------
PACKAGE_check_BUILD_TARGET       = check
PACKAGE_check_ARCHIVE_BASENAME   = check-0.9.5
PACKAGE_check_PRECONFIG_PATCHES  =
PACKAGE_check_CONFIGURE_OPTS     = ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes
PACKAGE_check_POSTCONFIG_PATCHES =
PACKAGE_check_BUILD_DIRS         =
PACKAGE_check_DESCRIPTION        = Check: a unit test framework for C
BASE_PACKAGES += $(PACKAGE_check_BUILD_TARGET)

#------------------------------------------------------------------------------
# Package gstreamer
#------------------------------------------------------------------------------
PACKAGE_gstreamer_BUILD_TARGET       = gstreamer
PACKAGE_gstreamer_ARCHIVE_BASENAME   = gstreamer-0.10.21
PACKAGE_gstreamer_PRECONFIG_PATCHES  = gstreamer2_0_10_21 gstreamer3_0_10_21
PACKAGE_gstreamer_CONFIGURE_OPTS     = ac_cv_func_register_printf_function=no --disable-gtk-doc --disable-loadsave --disable-tests --with-checklibname=check --disable-valgrind CFLAGS=-I"$(TARGET_GSTREAMER_DIR)/include"
PACKAGE_gstreamer_POSTCONFIG_PATCHES =
PACKAGE_gstreamer_BUILD_DIRS         =
PACKAGE_gstreamer_DESCRIPTION        = GStreamer library
BASE_PACKAGES += $(PACKAGE_gstreamer_BUILD_TARGET)

#------------------------------------------------------------------------------
# Package liboil
#------------------------------------------------------------------------------
PACKAGE_liboil_BUILD_TARGET       = liboil
PACKAGE_liboil_ARCHIVE_BASENAME   = liboil-0.3.15
PACKAGE_liboil_PRECONFIG_PATCHES  =
PACKAGE_liboil_CONFIGURE_OPTS     =
PACKAGE_liboil_POSTCONFIG_PATCHES =
PACKAGE_liboil_BUILD_DIRS         =
PACKAGE_liboil_DESCRIPTION        = Liboil library
BASE_PACKAGES += $(PACKAGE_liboil_BUILD_TARGET)

#------------------------------------------------------------------------------
# Package libid3tag
#------------------------------------------------------------------------------
PACKAGE_id3tag_BUILD_TARGET        = id3tag
PACKAGE_id3tag_ARCHIVE_BASENAME    = libid3tag-0.15.1b
PACKAGE_id3tag_PRECONFIG_PATCHES   = libid3tag1_0_15_1b
ifeq ($(PLATFORM), omap3530)
PACKAGE_id3tag_CONFIGURE_OPTS      = CPPFLAGS="-I$(TARGET_ROOT_DIR)/include" LDFLAGS="-L$(TARGET_ROOT_DIR)/lib"
endif
PACKAGE_id3tag_POSTCONFIG_PATCHES  =
PACKAGE_id3tag_DESCRIPTION         = id3 tag library
BASE_PACKAGES += $(PACKAGE_id3tag_BUILD_TARGET)

#------------------------------------------------------------------------------
# Package libmad
#------------------------------------------------------------------------------
PACKAGE_mad_BUILD_TARGET           = mad
PACKAGE_mad_ARCHIVE_BASENAME       = libmad-0.15.1b
PACKAGE_mad_PRECONFIG_PATCHES      = libmad1_0_15_1b
PACKAGE_mad_POSTCONFIG_PATCHES     =
PACKAGE_mad_DESCRIPTION            = mpeg audio decoder library
BASE_PACKAGES += $(PACKAGE_mad_BUILD_TARGET)

#------------------------------------------------------------------------------
# Package plugins_base
#------------------------------------------------------------------------------
PACKAGE_plugins_base_BUILD_TARGET       = plugins_base
PACKAGE_plugins_base_ARCHIVE_BASENAME   = gst-plugins-base-0.10.21
PACKAGE_plugins_base_PRECONFIG_PATCHES  = plugins_base1_0_10_21
PACKAGE_plugins_base_CONFIGURE_OPTS     = --disable-x --with-checklibname=check --disable-ogg --disable-pango --disable-vorbis --disable-examples --disable-gnome_vfs $(ALSA_SUPPORT)
PACKAGE_plugins_base_POSTCONFIG_PATCHES =
PACKAGE_plugins_base_BUILD_DIRS         =
PACKAGE_plugins_base_DESCRIPTION        = GStreamer plugins base library
BASE_PACKAGES += $(PACKAGE_plugins_base_BUILD_TARGET)

#------------------------------------------------------------------------------
# Package flumpegdemux
#------------------------------------------------------------------------------
PACKAGE_flumpegdemux_BUILD_TARGET       = flumpegdemux
PACKAGE_flumpegdemux_ARCHIVE_BASENAME   = gst-fluendo-mpegdemux-trunk.1483
PACKAGE_flumpegdemux_PRECONFIG_PATCHES  = flumpegdemux1_r1483 flumpegdemux2_r1483
PACKAGE_flumpegdemux_CONFIGURE_OPTS     =
PACKAGE_flumpegdemux_POSTCONFIG_PATCHES =
PACKAGE_flumpegdemux_BUILD_DIRS         =
PACKAGE_flumpegdemux_DESCRIPTION        = MPEG demuxers plugin
PLUGIN_PACKAGES += $(PACKAGE_flumpegdemux_BUILD_TARGET)

#------------------------------------------------------------------------------
# Package plugins_good
#------------------------------------------------------------------------------
PACKAGE_plugins_good_BUILD_TARGET        = plugins_good
PACKAGE_plugins_good_ARCHIVE_BASENAME    = gst-plugins-good-0.10.10
PACKAGE_plugins_good_PRECONFIG_PATCHES   = plugins_good1_0_10_10 plugins_good2_0_10_10
PACKAGE_plugins_good_CONFIGURE_OPTS      =
PACKAGE_plugins_good_POSTCONFIG_PATCHES  =
PACKAGE_plugins_good_BUILD_DIRS          = gst/avi
PACKAGE_plugins_good_BUILD_DIRS         += gst/qtdemux
PACKAGE_plugins_good_BUILD_DIRS         += sys/oss
PACKAGE_plugins_good_DESCRIPTION         = \
    Select plugins from GStreamer good-plugins (avi, oss)
PLUGIN_PACKAGES += $(PACKAGE_plugins_good_BUILD_TARGET)

#-------------------------------------------------------------------------------
# Package plugins_ugly
#-------------------------------------------------------------------------------
PACKAGE_plugins_ugly_BUILD_TARGET       = plugins_ugly
PACKAGE_plugins_ugly_ARCHIVE_BASENAME   = gst-plugins-ugly-0.10.9
PACKAGE_plugins_ugly_PRECONFIG_PATCHES  = 
ifeq ($(ALSA_SUPPORT), --disable-alsa)
    PACKAGE_plugins_ugly_PRECONFIG_PATCHES += plugins_ugly1_0_10_9
endif
PACKAGE_plugins_ugly_CONFIGURE_OPTS     = LDFLAGS=-L$(TARGET_ROOT_DIR)/lib
PACKAGE_plugins_ugly_POSTCONFIG_PATCHES =
PACKAGE_plugins_ugly_BUILD_DIRS         = ext/mad
PACKAGE_plugins_ugly_DESCRIPTION        = \
        Selected plugins from Gstreamer ugly-plugins (id3tag, mad)
PLUGIN_PACKAGES += $(PACKAGE_plugins_ugly_BUILD_TARGET)

