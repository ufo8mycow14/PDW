# CMakeLists.txt
#
# Build script for building with cmake (http://www.cmake.org)

set( PDW_INCLUDES
  ${PDW_INCLUDES}
  ${CMAKE_CURRENT_SOURCE_DIR}/Resource.h
  ${CMAKE_CURRENT_SOURCE_DIR}/SLICER.H
  ${CMAKE_CURRENT_SOURCE_DIR}/acars.h
  ${CMAKE_CURRENT_SOURCE_DIR}/config_backup.h
  ${CMAKE_CURRENT_SOURCE_DIR}/decode.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ermes.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ftp.h
  ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
  ${CMAKE_CURRENT_SOURCE_DIR}/helper_funcs.h
  #${CMAKE_CURRENT_SOURCE_DIR}/html.h
  ${CMAKE_CURRENT_SOURCE_DIR}/initapp.h
  ${CMAKE_CURRENT_SOURCE_DIR}/language.h
  ${CMAKE_CURRENT_SOURCE_DIR}/live_signal_meter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/menu.h
  ${CMAKE_CURRENT_SOURCE_DIR}/misc.h
  ${CMAKE_CURRENT_SOURCE_DIR}/mobitex.h
	${CMAKE_CURRENT_SOURCE_DIR}/message_router.h
  ${CMAKE_CURRENT_SOURCE_DIR}/notification.h
  ${CMAKE_CURRENT_SOURCE_DIR}/pdw.h
  ${CMAKE_CURRENT_SOURCE_DIR}/printer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/sigind.h
  ${CMAKE_CURRENT_SOURCE_DIR}/sound_in.h
  ${CMAKE_CURRENT_SOURCE_DIR}/settings_center.h
  ${CMAKE_CURRENT_SOURCE_DIR}/status_bar.h
  ${CMAKE_CURRENT_SOURCE_DIR}/startup.h
  ${CMAKE_CURRENT_SOURCE_DIR}/toolbar.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ui_theme.h
  ${CMAKE_CURRENT_SOURCE_DIR}/version.h
  
  PARENT_SCOPE
)
