# Base build script for PDW.

cmake_minimum_required(VERSION 3.20)

# version.h is the canonical version source for the executable, resources,
# and build system.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Headers/version.h" PDW_VERSION_DEFINES
  REGEX "^#define PDW_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Headers/version.h" PDW_DISPLAY_DEFINE
  REGEX "^#define PDW_DISPLAY_VERSION \"[^\"]+\"$")

foreach(PART MAJOR MINOR PATCH)
  foreach(VERSION_DEFINE IN LISTS PDW_VERSION_DEFINES)
    if(VERSION_DEFINE MATCHES "^#define PDW_VERSION_${PART} ([0-9]+)$")
      set(PDW_VERSION_${PART} "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endforeach()

if(NOT DEFINED PDW_VERSION_MAJOR OR
   NOT DEFINED PDW_VERSION_MINOR OR
   NOT DEFINED PDW_VERSION_PATCH OR
   NOT PDW_DISPLAY_DEFINE MATCHES "^#define PDW_DISPLAY_VERSION \"([^\"]+)\"$")
  message(FATAL_ERROR "Unable to read the PDW version from Headers/version.h")
endif()
set(PDW_DISPLAY_VERSION "${CMAKE_MATCH_1}")

project(PDW
  VERSION "${PDW_VERSION_MAJOR}.${PDW_VERSION_MINOR}.${PDW_VERSION_PATCH}"
  LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "PDW currently targets Windows only.")
endif()

if(NOT MSVC)
  message(FATAL_ERROR "PDW currently requires the Microsoft Visual C++ toolchain.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(PDW_TARGET_ARCH "x64")
else()
  set(PDW_TARGET_ARCH "x86")
endif()
message(STATUS "Configuring PDW ${PROJECT_VERSION} for ${PDW_TARGET_ARCH}")

set(PDW_GFXS)
set(PDW_INCLUDES)
set(PDW_SRCS)

add_subdirectory(GFX)
add_subdirectory(Headers)
add_subdirectory(utils)

set(PDW_DEPENDENCY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/out/dependencies/${PDW_TARGET_ARCH}"
  CACHE PATH "Path to the dependency set built by scripts/build-dependencies.ps1")

if(NOT EXISTS "${PDW_DEPENDENCY_ROOT}/include/openssl/ssl.h" OR
   NOT EXISTS "${PDW_DEPENDENCY_ROOT}/include/curl/curl.h")
  message(FATAL_ERROR
    "PDW dependencies were not found under ${PDW_DEPENDENCY_ROOT}. "
    "Run scripts/build-dependencies.ps1 -Architecture ${PDW_TARGET_ARCH} before configuring PDW.")
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
  live_signal_meter.cpp
  settings_center.cpp
  status_bar.cpp
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
  utils/config_backup.cpp
  utils/config_backup_core.cpp
  utils/data_outputs.cpp
  utils/decoded_event.cpp
  utils/flex_fragment_reassembly_core.cpp
  utils/local_dashboard_server.cpp
  utils/message_archive.cpp
  utils/message_archive_manager.cpp
  utils/message_centre.cpp
  utils/multi_channel.cpp
  utils/multi_channel_core.cpp
  utils/multi_channel_manager.cpp
  utils/output_health.cpp
  utils/publishing_delivery_state.cpp
  utils/publishing_job_store.cpp
	utils/rtl_signal_conditioner.cpp
  PROPERTIES COMPILE_OPTIONS "/W4;/WX"
)

set_target_properties(PDW PROPERTIES OUTPUT_NAME "${PDW_DISPLAY_VERSION}")

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

# Keep F1 help functional in a normal developer build as well as in the
# portable package, which already stages the manual under this legacy name.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pdw-manual.pdf")
	add_custom_command(TARGET PDW POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/pdw-manual.pdf"
			"$<TARGET_FILE_DIR:PDW>/PDW.pdf"
		COMMENT "Copying PDW manual for F1 help"
	)
endif()

include(CTest)

if(BUILD_TESTING)
	add_executable(PDWConfigBackupCoreTests
		tests/config_backup_core_tests.cpp
		utils/config_backup_core.cpp
	)
	target_include_directories(PDWConfigBackupCoreTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
		"${PDW_DEPENDENCY_ROOT}/include"
	)
	target_compile_options(PDWConfigBackupCoreTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWConfigBackupCoreTests PRIVATE
		"${PDW_OPENSSL_CRYPTO_LIBRARY}"
		Crypt32.lib
		Ws2_32.lib
	)
	add_test(NAME configuration-backup-core COMMAND PDWConfigBackupCoreTests)

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

	add_executable(PDWDecodedEventTests
		tests/decoded_event_tests.cpp
		utils/decoded_event.cpp
		utils/publishing_core.cpp
	)
	target_include_directories(PDWDecodedEventTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
		"${CMAKE_CURRENT_SOURCE_DIR}/Headers"
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWDecodedEventTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWDecodedEventTests PRIVATE Ole32.lib)
	add_test(NAME decoded-event-identity COMMAND PDWDecodedEventTests)

	# Compile the established POCSAG implementation unchanged against a
	# synthetic raw-codeword fixture. Test-only globals capture its output
	# without linking the Win32 presentation or network destinations.
	add_executable(PDWPocsagFixtureTests
		tests/pocsag_fixture_tests.cpp
		tests/pocsag_test_environment.cpp
		Pocsag.cpp
	)
	target_include_directories(PDWPocsagFixtureTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
		"${CMAKE_CURRENT_SOURCE_DIR}/Headers"
		"${CMAKE_CURRENT_SOURCE_DIR}/tests"
	)
	target_compile_definitions(PDWPocsagFixtureTests PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(PDWPocsagFixtureTests PRIVATE
		/W4 /WX /wd4244
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME pocsag-fixture-regression COMMAND PDWPocsagFixtureTests
		"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pocsag")

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
		utils/rtl_signal_conditioner.cpp
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
	add_test(NAME rtl-tcp-core COMMAND PDWRtlTcpTests $<TARGET_FILE:PDWMockRtlSdr>)

	add_library(PDWMockRtlSdr SHARED
		tests/mock_rtlsdr.cpp
	)
	add_dependencies(PDWRtlTcpTests PDWMockRtlSdr)
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

	add_executable(PDWPublishingJobStoreTests
		tests/publishing_job_store_tests.cpp
		utils/publishing_core.cpp
		utils/publishing_job_store.cpp
	)
	target_include_directories(PDWPublishingJobStoreTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_definitions(PDWPublishingJobStoreTests PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(PDWPublishingJobStoreTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME publishing-job-store COMMAND PDWPublishingJobStoreTests)

	add_executable(PDWPublishingDeliveryStateTests
		tests/publishing_delivery_state_tests.cpp
		utils/publishing_delivery_state.cpp
	)
	target_include_directories(PDWPublishingDeliveryStateTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWPublishingDeliveryStateTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME publishing-delivery-state COMMAND PDWPublishingDeliveryStateTests)

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

	add_executable(PDWMessageArchiveTests
		tests/message_archive_tests.cpp
		utils/message_archive.cpp
		utils/publishing_core.cpp
	)
	target_include_directories(PDWMessageArchiveTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWMessageArchiveTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWMessageArchiveTests PRIVATE Winsqlite3.lib)
	add_test(NAME message-archive COMMAND PDWMessageArchiveTests)

	add_executable(PDWMessageArchiveManagerTests
		tests/message_archive_manager_tests.cpp
		utils/local_dashboard_server.cpp
		utils/message_archive.cpp
		utils/message_archive_manager.cpp
		utils/publishing_core.cpp
	)
	target_include_directories(PDWMessageArchiveManagerTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
		"${CMAKE_CURRENT_SOURCE_DIR}/Headers"
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
target_compile_definitions(PDWMessageArchiveManagerTests PRIVATE
	_CRT_SECURE_NO_WARNINGS
	PDW_MESSAGE_ARCHIVE_MANAGER_TEST_HOOKS
)
	target_compile_options(PDWMessageArchiveManagerTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWMessageArchiveManagerTests PRIVATE Winsqlite3.lib Ws2_32.lib)
	add_test(NAME message-archive-manager COMMAND PDWMessageArchiveManagerTests)

	add_executable(PDWLocalDashboardCoreTests
		tests/local_dashboard_core_tests.cpp
		utils/local_dashboard_server.cpp
	)
	target_include_directories(PDWLocalDashboardCoreTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}"
		"${CMAKE_CURRENT_SOURCE_DIR}/Headers"
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWLocalDashboardCoreTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWLocalDashboardCoreTests PRIVATE Ws2_32.lib)
	add_test(NAME local-dashboard-core COMMAND PDWLocalDashboardCoreTests)

	add_executable(PDWMultiChannelCoreTests
		tests/multi_channel_core_tests.cpp
		utils/multi_channel_core.cpp
	)
	target_include_directories(PDWMultiChannelCoreTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWMultiChannelCoreTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME multi-channel-core COMMAND PDWMultiChannelCoreTests)

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

	add_executable(PDWReleaseDefaultsTests
		tests/release_defaults_tests.cpp
	)
	target_compile_options(PDWReleaseDefaultsTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME release-defaults COMMAND PDWReleaseDefaultsTests
		"${CMAKE_CURRENT_SOURCE_DIR}/packaging/PDW.INI")

	add_executable(PDWFlexFragmentReassemblyTests
		tests/flex_fragment_reassembly_core_tests.cpp
		utils/flex_fragment_reassembly_core.cpp
	)
	target_include_directories(PDWFlexFragmentReassemblyTests PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	)
	target_compile_options(PDWFlexFragmentReassemblyTests PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	add_test(NAME flex-fragment-reassembly COMMAND PDWFlexFragmentReassemblyTests)

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

	add_executable(PDWWinmmDeviceSmoke EXCLUDE_FROM_ALL
		tests/winmm_device_smoke.cpp
	)
	target_compile_options(PDWWinmmDeviceSmoke PRIVATE
		/W4 /WX
		"$<$<COMPILE_LANGUAGE:CXX>:/source-charset:utf-8>"
		"$<$<COMPILE_LANGUAGE:CXX>:/execution-charset:.1252>"
	)
	target_link_libraries(PDWWinmmDeviceSmoke PRIVATE Winmm.lib)
endif()
