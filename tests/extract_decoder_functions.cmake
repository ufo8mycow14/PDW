# The legacy acquisition code shares translation units with the native UI.
# Compile its actual function bodies in the synthetic harness, rather than
# maintaining decoder copies. All selected definitions use column-zero closing
# braces; fail configuration if a signature or that boundary changes.
function(pdw_fixture_functions output source)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
  file(READ "${source}" content)
  string(REPLACE "\r\n" "\n" content "${content}")
  set(result "// Generated from ${source}; do not edit.\n")
  foreach(signature IN LISTS ARGN)
    string(FIND "${content}" "\n${signature}" begin)
    if(begin LESS 0)
      message(FATAL_ERROR "Decoder fixture signature missing: ${signature}")
    endif()
    string(SUBSTRING "${content}" ${begin} -1 tail)
    string(REGEX MATCH "\n}[ \t]*(//[^\n]*)?\n" closing "${tail}")
    if(closing STREQUAL "")
      message(FATAL_ERROR "Decoder fixture boundary missing: ${signature}")
    endif()
    string(FIND "${tail}" "${closing}" end)
    string(LENGTH "${closing}" closing_length)
    math(EXPR length "${end} + ${closing_length}")
    string(SUBSTRING "${tail}" 0 ${length} body)
    string(APPEND result "${body}\n")
  endforeach()
  file(WRITE "${output}" "${result}")
endfunction()

set(fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/decoder-fixtures")
file(MAKE_DIRECTORY "${fixture_dir}")
pdw_fixture_functions("${fixture_dir}/bch.inc" "${CMAKE_CURRENT_SOURCE_DIR}/Misc.cpp"
  "int nOnes(" "int bit10(" "int ecd(" "void setupecc(" "void InvertData(")
pdw_fixture_functions("${fixture_dir}/flex.inc" "${CMAKE_CURRENT_SOURCE_DIR}/Flex.cpp"
  "void flex_reset(" "void FLEX::showblock(" "void frame_flex(")
pdw_fixture_functions("${fixture_dir}/flex-message.inc" "${CMAKE_CURRENT_SOURCE_DIR}/Flex.cpp"
  "int FLEX::xsumchk(" "void FLEX::show_address(" "void FLEX::show_phase_speed(" "void FLEX::showframe(")
pdw_fixture_functions("${fixture_dir}/flex-status.inc" "${CMAKE_CURRENT_SOURCE_DIR}/Flex.cpp"
  "void display_cfstatus(")
pdw_fixture_functions("${fixture_dir}/slicer.inc" "${CMAKE_CURRENT_SOURCE_DIR}/sound_in.cpp"
  "void Reset_ATB(" "void Audio_To_Bits(" "void SetAudioConfig(")
