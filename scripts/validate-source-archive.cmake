function(pdw_validate_source_archive SOURCE_DIRECTORY OUTPUT_COMMIT)
  set(PDW_ARCHIVE_PROVENANCE
    "${SOURCE_DIRECTORY}/PDW_SOURCE_PROVENANCE.txt")
  set(PDW_ARCHIVE_HASHES
    "${SOURCE_DIRECTORY}/PDW_SOURCE_SHA256SUMS.txt")
  if(NOT EXISTS "${PDW_ARCHIVE_PROVENANCE}" OR
     NOT EXISTS "${PDW_ARCHIVE_HASHES}")
    message(FATAL_ERROR
      "This source tree is not a Git checkout and has no validated PDW release-source provenance.")
  endif()

  file(READ "${PDW_ARCHIVE_PROVENANCE}" PDW_ARCHIVE_PROVENANCE_TEXT)
  string(REPLACE "\r\n" "\n" PDW_ARCHIVE_PROVENANCE_TEXT
    "${PDW_ARCHIVE_PROVENANCE_TEXT}")
  string(REPLACE "\r" "\n" PDW_ARCHIVE_PROVENANCE_TEXT
    "${PDW_ARCHIVE_PROVENANCE_TEXT}")
  if(NOT PDW_ARCHIVE_PROVENANCE_TEXT MATCHES
      "^commit=([0-9a-fA-F]+)\nstate=clean\norigin=git-archive\n$")
    message(FATAL_ERROR "Release-source provenance metadata is malformed.")
  endif()
  set(PDW_ARCHIVE_COMMIT "${CMAKE_MATCH_1}")
  string(LENGTH "${PDW_ARCHIVE_COMMIT}" PDW_ARCHIVE_COMMIT_LENGTH)
  if(NOT PDW_ARCHIVE_COMMIT_LENGTH EQUAL 40)
    message(FATAL_ERROR "Release-source provenance has an invalid commit ID.")
  endif()
  string(TOLOWER "${PDW_ARCHIVE_COMMIT}" PDW_ARCHIVE_COMMIT)

  file(STRINGS "${PDW_ARCHIVE_HASHES}" PDW_ARCHIVE_HASH_LINES
    ENCODING UTF-8)
  if(NOT PDW_ARCHIVE_HASH_LINES)
    message(FATAL_ERROR "Release-source SHA-256 manifest is empty.")
  endif()
  set(PDW_ARCHIVE_SEEN_PROVENANCE FALSE)
  set(PDW_ARCHIVE_PATHS)
  foreach(PDW_ARCHIVE_HASH_LINE IN LISTS PDW_ARCHIVE_HASH_LINES)
    if(NOT PDW_ARCHIVE_HASH_LINE MATCHES "^([0-9a-fA-F]+)  (.+)$")
      message(FATAL_ERROR "Release-source SHA-256 manifest has a malformed line.")
    endif()
    set(PDW_ARCHIVE_EXPECTED_HASH "${CMAKE_MATCH_1}")
    set(PDW_ARCHIVE_RELATIVE_PATH "${CMAKE_MATCH_2}")
    string(LENGTH "${PDW_ARCHIVE_EXPECTED_HASH}" PDW_ARCHIVE_HASH_LENGTH)
    if(NOT PDW_ARCHIVE_HASH_LENGTH EQUAL 64 OR
       PDW_ARCHIVE_RELATIVE_PATH MATCHES "(^|/)\.\.(/|$)" OR
       PDW_ARCHIVE_RELATIVE_PATH MATCHES "^[\\/]" OR
       PDW_ARCHIVE_RELATIVE_PATH MATCHES "[\\:;]" OR
       PDW_ARCHIVE_RELATIVE_PATH STREQUAL "PDW_SOURCE_SHA256SUMS.txt")
      message(FATAL_ERROR "Release-source SHA-256 manifest contains an unsafe entry.")
    endif()
    list(FIND PDW_ARCHIVE_PATHS "${PDW_ARCHIVE_RELATIVE_PATH}"
      PDW_ARCHIVE_DUPLICATE_INDEX)
    if(NOT PDW_ARCHIVE_DUPLICATE_INDEX EQUAL -1)
      message(FATAL_ERROR
        "Release-source SHA-256 manifest repeats '${PDW_ARCHIVE_RELATIVE_PATH}'.")
    endif()
    list(APPEND PDW_ARCHIVE_PATHS "${PDW_ARCHIVE_RELATIVE_PATH}")

    set(PDW_ARCHIVE_FILE
      "${SOURCE_DIRECTORY}/${PDW_ARCHIVE_RELATIVE_PATH}")
    if(NOT EXISTS "${PDW_ARCHIVE_FILE}" OR IS_DIRECTORY "${PDW_ARCHIVE_FILE}")
      message(FATAL_ERROR
        "Release-source file is missing: ${PDW_ARCHIVE_RELATIVE_PATH}")
    endif()
    file(SHA256 "${PDW_ARCHIVE_FILE}" PDW_ARCHIVE_ACTUAL_HASH)
    string(TOLOWER "${PDW_ARCHIVE_EXPECTED_HASH}"
      PDW_ARCHIVE_EXPECTED_HASH)
    string(TOLOWER "${PDW_ARCHIVE_ACTUAL_HASH}" PDW_ARCHIVE_ACTUAL_HASH)
    if(NOT PDW_ARCHIVE_ACTUAL_HASH STREQUAL PDW_ARCHIVE_EXPECTED_HASH)
      message(FATAL_ERROR
        "Release-source file failed SHA-256 validation: ${PDW_ARCHIVE_RELATIVE_PATH}")
    endif()
    if(PDW_ARCHIVE_RELATIVE_PATH STREQUAL "PDW_SOURCE_PROVENANCE.txt")
      set(PDW_ARCHIVE_SEEN_PROVENANCE TRUE)
    endif()
  endforeach()
  if(NOT PDW_ARCHIVE_SEEN_PROVENANCE)
    message(FATAL_ERROR
      "Release-source SHA-256 manifest does not bind its provenance metadata.")
  endif()

  file(GLOB_RECURSE PDW_ARCHIVE_PRESENT_PATHS
    LIST_DIRECTORIES FALSE
    RELATIVE "${SOURCE_DIRECTORY}"
    "${SOURCE_DIRECTORY}/*")
  list(REMOVE_ITEM PDW_ARCHIVE_PRESENT_PATHS "PDW_SOURCE_SHA256SUMS.txt")
  list(SORT PDW_ARCHIVE_PRESENT_PATHS)
  list(SORT PDW_ARCHIVE_PATHS)
  if(NOT "${PDW_ARCHIVE_PRESENT_PATHS}" STREQUAL "${PDW_ARCHIVE_PATHS}")
    message(FATAL_ERROR
      "Release-source tree contains an unlisted file or omits a manifested file. Build outside the extracted Source directory.")
  endif()

  set(${OUTPUT_COMMIT} "${PDW_ARCHIVE_COMMIT}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(NOT DEFINED PDW_SOURCE_DIRECTORY)
    message(FATAL_ERROR "PDW_SOURCE_DIRECTORY is required for archive validation.")
  endif()
  pdw_validate_source_archive("${PDW_SOURCE_DIRECTORY}"
    PDW_VALIDATED_ARCHIVE_COMMIT)
  message(STATUS
    "Validated PDW release-source archive ${PDW_VALIDATED_ARCHIVE_COMMIT}")
endif()
