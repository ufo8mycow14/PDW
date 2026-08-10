# CMakeLists.txt
#
# Build script for building with cmake (http://www.cmake.org)

set( PDW_INCLUDES
  ${PDW_INCLUDES}
  ${CMAKE_CURRENT_SOURCE_DIR}/OSTYPE.H
  ${CMAKE_CURRENT_SOURCE_DIR}/binary.h
  #${CMAKE_CURRENT_SOURCE_DIR}/debug.h
  #${CMAKE_CURRENT_SOURCE_DIR}/globals.h
  #${CMAKE_CURRENT_SOURCE_DIR}/ipcnfg.h
  ${CMAKE_CURRENT_SOURCE_DIR}/notification_core.h
	${CMAKE_CURRENT_SOURCE_DIR}/publishing_core.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rs232.h
  ${CMAKE_CURRENT_SOURCE_DIR}/smtp.h
  ${CMAKE_CURRENT_SOURCE_DIR}/smtp_int.h
  
  PARENT_SCOPE
)

set( PDW_SRCS
	${PDW_SRCS}
	${CMAKE_CURRENT_SOURCE_DIR}/audio_signal_core.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/curl_runtime.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/data_output_core.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/data_outputs.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/decoded_event.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/decoder_candidate_core.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/flex_fragment_reassembly_core.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/signal_recording_core.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/signal_diagnostics.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/wasapi_capture.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/rtl_tcp_source.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/receiver_catalog.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/ini_preservation_core.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/message_router.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/mysql_odbc_output.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/output_health.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/output_health_core.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/publishing.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/publishing_core.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/smtp_message_core.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/sqlite_output.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/telnet_output_server.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/windows_toast.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Debug.cpp
  #${CMAKE_CURRENT_SOURCE_DIR}/OSTYPE.C
  ${CMAKE_CURRENT_SOURCE_DIR}/Ostype.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/binary.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ftp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ipcnfg.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/notification.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/notification_core.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/rs232.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/smtp.cpp
  
  PARENT_SCOPE
)

