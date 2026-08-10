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
)

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
