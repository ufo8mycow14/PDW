# Base build script for PDW.

cmake_minimum_required(VERSION 3.20)

# version.h is the canonical version source for the executable, resources,
# and build system.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Headers/version.h" PDW_VERSION_DEFINES
  REGEX "^#define PDW_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")

foreach(PART MAJOR MINOR PATCH)
  foreach(VERSION_DEFINE IN LISTS PDW_VERSION_DEFINES)
    if(VERSION_DEFINE MATCHES "^#define PDW_VERSION_${PART} ([0-9]+)$")
      set(PDW_VERSION_${PART} "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endforeach()

if(NOT DEFINED PDW_VERSION_MAJOR OR
   NOT DEFINED PDW_VERSION_MINOR OR
   NOT DEFINED PDW_VERSION_PATCH)
  message(FATAL_ERROR "Unable to read the PDW version from Headers/version.h")
endif()

project(PDW
  VERSION "${PDW_VERSION_MAJOR}.${PDW_VERSION_MINOR}.${PDW_VERSION_PATCH}"
  LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "PDW currently targets Windows only.")
endif()

if(NOT MSVC)
  message(FATAL_ERROR "PDW currently requires the Microsoft Visual C++ toolchain.")
endif()

# The legacy slicer/serial integration currently requires an x86 build.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "PDW must be built as 32-bit (Win32). Use -A Win32 with CMake.")
endif()

set(PDW_GFXS)
set(PDW_INCLUDES)
set(PDW_SRCS)

add_subdirectory(GFX)
add_subdirectory(Headers)
add_subdirectory(utils)

set(PDW_DEPENDENCY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/out/dependencies/x86"
  CACHE PATH "Path to the dependency set built by scripts/build-dependencies.ps1")

if(NOT EXISTS "${PDW_DEPENDENCY_ROOT}/include/openssl/ssl.h" OR
   NOT EXISTS "${PDW_DEPENDENCY_ROOT}/include/curl/curl.h")
  message(FATAL_ERROR
    "PDW dependencies were not found under ${PDW_DEPENDENCY_ROOT}. "
    "Run scripts/build-dependencies.ps1 before configuring PDW.")
endif()

file(STRINGS "${PDW_DEPENDENCY_ROOT}/include/openssl/opensslv.h" PDW_OPENSSL_VERSION_LINE
  REGEX "^# *define +OPENSSL_VERSION_STR +\"[0-9]+\\.[0-9]+\\.[0-9]+\"$")
if(NOT PDW_OPENSSL_VERSION_LINE MATCHES "\"3\\.5\\.7\"$")
  message(FATAL_ERROR "PDW requires the pinned OpenSSL 3.5.7 dependency set.")
endif()

list(PREPEND CMAKE_PREFIX_PATH "${PDW_DEPENDENCY_ROOT}")
set(CURL_USE_STATIC_LIBS TRUE)
set(LIBSSH2_USE_STATIC_LIBS TRUE)
find_package(CURL 8.21.0 EXACT CONFIG REQUIRED
  PATHS "${PDW_DEPENDENCY_ROOT}/lib/cmake/CURL"
  NO_DEFAULT_PATH)

foreach(PDW_TRANSFER_PROTOCOL FTP FTPS SFTP)
  if(NOT CURL_SUPPORTS_${PDW_TRANSFER_PROTOCOL})
    message(FATAL_ERROR
      "The pinned libcurl dependency does not include ${PDW_TRANSFER_PROTOCOL} support.")
  endif()
endforeach()

foreach(PDW_MQTT_PROTOCOL MQTT MQTTS)
  if(NOT CURL_SUPPORTS_${PDW_MQTT_PROTOCOL})
    message(FATAL_ERROR
      "The pinned libcurl dependency does not include ${PDW_MQTT_PROTOCOL} support.")
  endif()
endforeach()

find_library(PDW_OPENSSL_SSL_LIBRARY NAMES libssl ssl
  PATHS "${PDW_DEPENDENCY_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_library(PDW_OPENSSL_CRYPTO_LIBRARY NAMES libcrypto crypto
  PATHS "${PDW_DEPENDENCY_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)

list(APPEND PDW_INCLUDES resrc1.h)

list(APPEND PDW_SRCS
  Flex.cpp
  Gfx.cpp
  Initapp.cpp
  Misc.cpp
  Rsrc.rc
  PDW.cpp
  Pocsag.cpp
  acars.cpp
  decode.cpp
  ermes.cpp
  helper_funcs.cpp
  language.cpp
  menu.cpp
  mobitex.cpp
  toolbar.cpp
  ui_theme.cpp
  printer.cpp
  sigind.cpp
  sound_in.cpp
  startup.cpp
)

add_executable(PDW WIN32
  ${PDW_GFXS}
  ${PDW_INCLUDES}
  ${PDW_SRCS}
)

# New isolation/routing modules are warning-clean even though the untouched
# decoder translation units still require their historical warning profile.
set_source_files_properties(
  utils/data_outputs.cpp
  utils/decoded_event.cpp
  utils/output_health.cpp
  PROPERTIES COMPILE_OPTIONS "/W4;/WX"
)

set_target_properties(PDW PROPERTIES OUTPUT_NAME "PDW v4.1.0 Beta")

target_include_directories(PDW PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/Headers"
  "${CMAKE_CURRENT_SOURCE_DIR}/utils"
  "${PDW_DEPENDENCY_ROOT}/include"
)

target_compile_definitions(PDW PRIVATE _CRT_SECURE_NO_WARNINGS CURL_STATICLIB)
target_link_options(PDW PRIVATE "/MANIFEST:NO")

# Touched legacy files are stored as UTF-8, while PDW's narrow-character
# protocol/display markers intentionally remain Windows-1252 at runtime.
target_compile_options(PDW PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
  "$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
)

target_link_libraries(PDW PRIVATE
  Comctl32.lib
  Winmm.lib
  Ws2_32.lib
  CURL::libcurl
  "${PDW_OPENSSL_SSL_LIBRARY}"
  "${PDW_OPENSSL_CRYPTO_LIBRARY}"
  legacy_stdio_definitions.lib
  Crypt32.lib
  Advapi32.lib
  Gdi32.lib
  User32.lib
  Dwmapi.lib
  UxTheme.lib
	Shell32.lib
	Ole32.lib
	Mmdevapi.lib
	Uuid.lib
	Winsqlite3.lib
	Odbc32.lib
)

# Receiver support is deliberately data-driven and optional. Copying the
# catalogue beside each build keeps native radio DLLs together without adding
# a load-time dependency that could prevent legacy PDW inputs from starting.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Receivers")
	add_custom_command(TARGET PDW POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/Receivers"
			"$<TARGET_FILE_DIR:PDW>/Receivers"
		COMMENT "Copying optional receiver catalogue"
	)
endif()

include(CTest)

if(BUILD_TESTING)
  add_executable(PDWNotificationTests
    tests/notification_core_tests.cpp
    utils/notification_core.cpp
  )
  target_include_directories(PDWNotificationTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/utils"
  )
  target_compile_definitions(PDWNotificationTests PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(PDWNotificationTests PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
    "$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
  )
  add_test(NAME notification-core COMMAND PDWNotificationTests)

	add_executable(PDWAudioSignalTests
		tests/audio_signal_core_tests.cpp
		utils/audio_signal_core.cpp
	)
	target_include_directories(PDWAudioSignalTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWAudioSignalTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME audio-signal-core COMMAND PDWAudioSignalTests)

	add_executable(PDWDecoderCandidateTests
		tests/decoder_candidate_core_tests.cpp
		utils/decoder_candidate_core.cpp
	)
	target_include_directories(PDWDecoderCandidateTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWDecoderCandidateTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME decoder-candidate-core COMMAND PDWDecoderCandidateTests)

	add_executable(PDWSignalRecordingTests
		tests/signal_recording_core_tests.cpp
		utils/audio_signal_core.cpp
		utils/signal_recording_core.cpp
	)
	target_include_directories(PDWSignalRecordingTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWSignalRecordingTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME signal-recording-core COMMAND PDWSignalRecordingTests)

	add_executable(PDWSignalDiagnosticsTests
		tests/signal_diagnostics_tests.cpp
		utils/audio_signal_core.cpp
		utils/signal_diagnostics.cpp
	)
	target_include_directories(PDWSignalDiagnosticsTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWSignalDiagnosticsTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME signal-diagnostics-core COMMAND PDWSignalDiagnosticsTests)

	add_executable(PDWWasapiCaptureTests
		tests/wasapi_capture_tests.cpp
		utils/audio_signal_core.cpp
		utils/wasapi_capture.cpp
	)
	target_include_directories(PDWWasapiCaptureTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_definitions(PDWWasapiCaptureTests PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(PDWWasapiCaptureTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWWasapiCaptureTests PRIVATE Ole32.lib Mmdevapi.lib Uuid.lib)
	add_test(NAME wasapi-capture-core COMMAND PDWWasapiCaptureTests)

	add_executable(PDWRtlTcpTests
		tests/rtl_tcp_source_tests.cpp
		utils/audio_signal_core.cpp
		utils/rtl_tcp_source.cpp
	)
	target_include_directories(PDWRtlTcpTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_definitions(PDWRtlTcpTests PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(PDWRtlTcpTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWRtlTcpTests PRIVATE Ws2_32.lib)
	add_test(NAME rtl-tcp-core COMMAND PDWRtlTcpTests)

	add_library(PDWMockRtlSdr SHARED
		tests/mock_rtlsdr.cpp
	)
	set_target_properties(PDWMockRtlSdr PROPERTIES PREFIX "")

	add_executable(PDWReceiverCatalogTests
		tests/receiver_catalog_tests.cpp
		utils/receiver_catalog.cpp
	)
	add_dependencies(PDWReceiverCatalogTests PDWMockRtlSdr)
	target_include_directories(PDWReceiverCatalogTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_definitions(PDWReceiverCatalogTests PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(PDWReceiverCatalogTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWReceiverCatalogTests PRIVATE Shell32.lib)
	add_test(NAME receiver-catalog COMMAND PDWReceiverCatalogTests $<TARGET_FILE:PDWMockRtlSdr>)

	add_executable(PDWPublishingCoreTests
		tests/publishing_core_tests.cpp
		utils/publishing_core.cpp
	)
	target_include_directories(PDWPublishingCoreTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWPublishingCoreTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME publishing-core COMMAND PDWPublishingCoreTests)

	add_executable(PDWSmtpMessageCoreTests
		tests/smtp_message_core_tests.cpp
		utils/smtp_message_core.cpp
	)
	target_include_directories(PDWSmtpMessageCoreTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWSmtpMessageCoreTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME smtp-message-core COMMAND PDWSmtpMessageCoreTests)

	add_executable(PDWDataOutputCoreTests
		tests/data_output_core_tests.cpp
		utils/data_output_core.cpp
	)
	target_include_directories(PDWDataOutputCoreTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWDataOutputCoreTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME data-output-core COMMAND PDWDataOutputCoreTests)

	add_executable(PDWSQLiteOutputTests
		tests/sqlite_output_tests.cpp
		utils/data_output_core.cpp
		utils/publishing_core.cpp
		utils/sqlite_output.cpp
	)
	target_include_directories(PDWSQLiteOutputTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWSQLiteOutputTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWSQLiteOutputTests PRIVATE Winsqlite3.lib)
	add_test(NAME sqlite-output COMMAND PDWSQLiteOutputTests)

	add_executable(PDWTelnetOutputServerTests
		tests/telnet_output_server_tests.cpp
		utils/data_output_core.cpp
		utils/telnet_output_server.cpp
	)
	target_include_directories(PDWTelnetOutputServerTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_definitions(PDWTelnetOutputServerTests PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(PDWTelnetOutputServerTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWTelnetOutputServerTests PRIVATE Ws2_32.lib)
	add_test(NAME telnet-output-server COMMAND PDWTelnetOutputServerTests)

	add_executable(PDWWindowsToastTests
		tests/windows_toast_tests.cpp
		utils/windows_toast.cpp
	)
	target_include_directories(PDWWindowsToastTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWWindowsToastTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWWindowsToastTests PRIVATE Advapi32.lib Shell32.lib)
	add_test(NAME windows-toast-lazy COMMAND PDWWindowsToastTests)

	add_executable(PDWMysqlOdbcOutputTests
		tests/mysql_odbc_output_tests.cpp
		utils/data_output_core.cpp
		utils/mysql_odbc_output.cpp
		utils/publishing_core.cpp
	)
	target_include_directories(PDWMysqlOdbcOutputTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWMysqlOdbcOutputTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWMysqlOdbcOutputTests PRIVATE Odbc32.lib)
	add_test(NAME mysql-odbc-disabled COMMAND PDWMysqlOdbcOutputTests)

	add_executable(PDWOutputHealthCoreTests
		tests/output_health_core_tests.cpp
		utils/output_health_core.cpp
	)
	target_include_directories(PDWOutputHealthCoreTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWOutputHealthCoreTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME output-health-core COMMAND PDWOutputHealthCoreTests)

	add_executable(PDWIniPreservationTests
		tests/ini_preservation_core_tests.cpp
		utils/ini_preservation_core.cpp
	)
	target_include_directories(PDWIniPreservationTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWIniPreservationTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME ini-preservation COMMAND PDWIniPreservationTests)

	add_executable(PDWWasapiDeviceSmoke EXCLUDE_FROM_ALL
		tests/wasapi_device_smoke.cpp
		utils/audio_signal_core.cpp
		utils/wasapi_capture.cpp
	)
	target_include_directories(PDWWasapiDeviceSmoke PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWWasapiDeviceSmoke PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWWasapiDeviceSmoke PRIVATE Ole32.lib Mmdevapi.lib Uuid.lib)
endif()
