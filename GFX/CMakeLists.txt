# Resource assets embedded into PDW by Rsrc.rc. Listing them on the target
# keeps generated Visual Studio projects complete and makes the maintained
# asset set explicit for both Win32 and x64 builds.
set(PDW_GFXS
  ${PDW_GFXS}
  ${CMAKE_CURRENT_SOURCE_DIR}/clr.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/copy.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/cpyLO.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/cpyUP.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/exclamation.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/filter.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/help.BMP
  ${CMAKE_CURRENT_SOURCE_DIR}/mode.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/open.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/options.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/pause.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/pdw.ico
  ${CMAKE_CURRENT_SOURCE_DIR}/pdwlogo2.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/prt.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/save.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/sigind.bmp
  ${CMAKE_CURRENT_SOURCE_DIR}/stats.bmp

  PARENT_SCOPE
)
