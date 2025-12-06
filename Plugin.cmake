# ~~~
# Summary:      Local, non-generic plugin setup
# Copyright (c) 2020-2021 Mike Rossiter
# License:      GPLv3+
# ~~~
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.


# -------- Options ----------

set(OCPN_TEST_REPO
    "opencpn/vdr-alpha"
    CACHE STRING "Default repository for untagged builds"
)
set(OCPN_BETA_REPO
    "opencpn/vdr-beta"
    CACHE STRING "Default repository for tagged builds matching 'beta'"
)
set(OCPN_RELEASE_REPO
    "opencpn/vdr-prod"
    CACHE STRING "Default repository for tagged builds not matching 'beta'"
)

#
#
# -------  Plugin setup --------
#
set(PKG_NAME vdr_pi)
set(PKG_VERSION  1.4.00.0)
set(PKG_PRERELEASE "")  # Empty, or a tag like 'beta'

set(DISPLAY_NAME VDR)    # Dialogs, installer artifacts, ...
set(PLUGIN_API_NAME VDR) # As of GetCommonName() in plugin API
set(PKG_SUMMARY "Voyage Data Recorder")
set(PKG_DESCRIPTION [=[
A Voyage Data Recorder (VDR) to record and play NMEA files.
Save NMEA stream to a file.  Replay NMEA stream previously saved. Used to test plugins.
]=])

set(PKG_AUTHOR "Rick Gleason")
set(PKG_IS_OPEN_SOURCE "yes")
set(PKG_HOMEPAGE_URL https://github.com/rgleason/vdr_pi)
set(PKG_INFO_URL https://opencpn.org/OpenCPN/plugins/vdr.html)

set(SRC
  src/control_gui.h
  src/icons.h
  src/icons.cpp
  src/vdr_pi.h
  src/vdr_pi.cpp
  src/vdr_pi_prefs.h
  src/vdr_pi_prefs.cpp
  src/vdr_pi_control.h
  src/vdr_pi_control.cpp
  src/vdr_pi_prefs_net.h
  src/vdr_pi_prefs_net.cpp
  src/vdr_pi_time.h
  src/vdr_pi_time.cpp
  src/vdr_network.h
  src/vdr_network.cpp
  src/record_play_mgr.h
  src/record_play_mgr.cpp
  src/dm_replay_mgr.h
  src/dm_replay_mgr.cpp
)

set(PKG_API_LIB api-18)  #  A directory in libs/ e. g., api-18 or api-19

macro(add_plugin_libraries)
  add_subdirectory(${CMAKE_SOURCE_DIR}/libs/fast-cpp-csv-parser)
  target_link_libraries(${PACKAGE_NAME} csv-parser::csv-parser)
  add_subdirectory(${CMAKE_SOURCE_DIR}/libs/std_filesystem)
  target_link_libraries(${PACKAGE_NAME} ocpn::filesystem)
  add_subdirectory(${CMAKE_SOURCE_DIR}/opencpn-libs/plugin_dc)
  target_link_libraries(${PACKAGE_NAME} ocpn::plugin-dc)
  if (BUILD_TESTING)
    enable_testing()
    add_subdirectory(${CMAKE_SOURCE_DIR}/test)
  endif ()
endmacro ()
