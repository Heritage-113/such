# Text-only regression gate for Win32/MSVC defects already observed in Such.
# The real authority remains an actual MSVC build; this catches stale paths and
# known bad source patterns at configure time on every platform.
set(_such_win_src "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/Win32Frontend.cpp")
set(_such_runtime_src "${CMAKE_CURRENT_LIST_DIR}/../src/runtime/RuntimeClient.cpp")
set(_such_contract_test "${CMAKE_CURRENT_LIST_DIR}/../tests/public_contracts.cpp")
set(_such_runtime_test "${CMAKE_CURRENT_LIST_DIR}/../tests/runtime_client_smoke.cpp")
set(_such_search_dialect "${CMAKE_CURRENT_LIST_DIR}/../src/ui/SearchDialect.cpp")
set(_such_agent_launcher "${CMAKE_CURRENT_LIST_DIR}/../src/ui/AgentLauncher.cpp")
set(_such_fake_runtime "${CMAKE_CURRENT_LIST_DIR}/../tests/fake_runtime.cpp")
set(_such_cmake "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt")

foreach(_required
    "${_such_win_src}"
    "${_such_runtime_src}"
    "${_such_contract_test}"
    "${_such_runtime_test}"
    "${_such_search_dialect}"
    "${_such_agent_launcher}"
    "${_such_fake_runtime}"
    "${_such_cmake}")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Windows compile-hazard gate missing current split file: ${_required}")
  endif()
endforeach()

file(READ "${_such_win_src}" _such_win_text)
file(READ "${_such_runtime_src}" _such_runtime_text)
file(READ "${_such_contract_test}" _such_contract_text)
file(READ "${_such_runtime_test}" _such_runtime_test_text)
file(READ "${_such_search_dialect}" _such_search_dialect_text)
file(READ "${_such_agent_launcher}" _such_agent_launcher_text)
file(READ "${_such_fake_runtime}" _such_fake_runtime_text)
file(READ "${_such_cmake}" _such_cmake_text)
set(_such_test_text "${_such_contract_text}\n${_such_runtime_test_text}")

foreach(_forbidden
    "SIID_FAVORITES"
    "SetPointerCapture("
    "ReleasePointerCapture("
    "std::max(1, client.right"
    "std::max(1, client.bottom"
    "std::min(end, row.left"
    "std::max(start, row.right")
  string(FIND "${_such_win_text}" "${_forbidden}" _hit)
  if(NOT _hit EQUAL -1)
    message(FATAL_ERROR "Rejected known Win32/MSVC compile hazard '${_forbidden}'")
  endif()
endforeach()

string(FIND "${_such_test_text}" "CHECK(design::k" _runtime_const_check)
if(NOT _runtime_const_check EQUAL -1)
  message(FATAL_ERROR "Compile-time DesignContract constants must use static_assert, not runtime CHECK on MSVC /WX")
endif()
string(FIND "${_such_test_text}" "while (false)" _constant_loop)
if(NOT _constant_loop EQUAL -1)
  message(FATAL_ERROR "Rejected constant-condition test macro pattern that previously triggered MSVC C4127/C2220")
endif()
string(FIND "${_such_runtime_text}" "reinterpret_cast<Fn>(load_symbol" _unsafe_symbol_cast)
if(NOT _unsafe_symbol_cast EQUAL -1)
  message(FATAL_ERROR "Rejected C4191-prone direct runtime function-pointer cast")
endif()
string(FIND "${_such_cmake_text}" "test_v061_frontend.cpp" _stale_test_path)
if(NOT _stale_test_path EQUAL -1)
  message(FATAL_ERROR "Rejected stale pre-split Windows test path test_v061_frontend.cpp")
endif()
string(FIND "${_such_cmake_text}" "WINDOWS_EXPORT_ALL_SYMBOLS ON" _stub_exports)
if(_stub_exports EQUAL -1)
  message(FATAL_ERROR "Windows ABI test stub must export symbols for GetProcAddress")
endif()
string(FIND "${_such_win_text}" "HeritageSuchV100Window" _v100_class)
if(_v100_class EQUAL -1)
  message(FATAL_ERROR "Win32 window class must carry the v1.0 identity to avoid stale class collisions")
endif()
string(FIND "${_such_win_text}" "#include <such/Version.h>" _version_header)
if(_version_header EQUAL -1)
  message(FATAL_ERROR "Win32 product title must use the generated public Version.h")
endif()


# MSVC /W4 reports local-name reuse in chained if-init statements as C4456.
# Keep the portable compiler warning set aligned with this rule and reject the
# exact stale spellings that caused the v1.0 Windows build failure.
foreach(_shadow_pattern
    "else if (const auto range = parse_compact_range_local"
    "else if (const auto range = parse_compact_day_as_range"
    "const auto range = local_day_range(*d, 0)")
  string(FIND "${_such_search_dialect_text}" "${_shadow_pattern}" _shadow_hit)
  if(NOT _shadow_hit EQUAL -1)
    message(FATAL_ERROR "Rejected MSVC C4456-prone SearchDialect local name '${_shadow_pattern}'")
  endif()
endforeach()

# path.wstring() can bind through both path and wstring_view overloads on MSVC.
string(FIND "${_such_agent_launcher_text}" "return ps_quote(path.wstring());" _ambiguous_quote)
if(NOT _ambiguous_quote EQUAL -1)
  message(FATAL_ERROR "Rejected MSVC C2668-prone ps_quote(path.wstring()) overload")
endif()

# The test stub is also built with /WX. std::getenv is C4996 under MSVC, so
# test-only code must use the same secure Windows environment access discipline.
string(FIND "${_such_fake_runtime_text}" "std::getenv(\"SUCH_FAKE_RUNTIME_FAIL_" _unsafe_test_getenv)
if(NOT _unsafe_test_getenv EQUAL -1)
  message(FATAL_ERROR "Rejected MSVC C4996-prone std::getenv in fake runtime")
endif()

message(STATUS "Such Win32/MSVC known-hazard gate: PASS")
