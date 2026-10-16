# Text-only regression gate for Win32/MSVC defects already observed in Such.
# The real authority remains an actual MSVC build; this catches stale paths and
# known bad source patterns at configure time on every platform.
set(_such_win_src "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/Win32Frontend.cpp")
set(_such_runtime_src "${CMAKE_CURRENT_LIST_DIR}/../src/runtime/RuntimeClient.cpp")
set(_such_index_snapshot_src "${CMAKE_CURRENT_LIST_DIR}/../src/runtime/IndexSnapshot.cpp")
set(_such_index_policy_src "${CMAKE_CURRENT_LIST_DIR}/../src/runtime/IndexPathPolicy.cpp")
set(_such_process_control_src "${CMAKE_CURRENT_LIST_DIR}/../src/cli/ProcessControl.cpp")
set(_such_index_worker_launcher "${CMAKE_CURRENT_LIST_DIR}/../src/runtime/IndexWorkerLauncher.cpp")
set(_such_contract_test "${CMAKE_CURRENT_LIST_DIR}/../tests/public_contracts.cpp")
set(_such_runtime_test "${CMAKE_CURRENT_LIST_DIR}/../tests/runtime_client_smoke.cpp")
set(_such_search_dialect "${CMAKE_CURRENT_LIST_DIR}/../src/ui/SearchDialect.cpp")
set(_such_agent_launcher "${CMAKE_CURRENT_LIST_DIR}/../src/ui/AgentLauncher.cpp")
set(_such_fake_runtime "${CMAKE_CURRENT_LIST_DIR}/../tests/fake_runtime.cpp")
set(_such_cmake "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt")

foreach(_required
    "${_such_win_src}"
    "${_such_runtime_src}"
    "${_such_index_snapshot_src}"
    "${_such_index_policy_src}"
    "${_such_process_control_src}"
    "${_such_index_worker_launcher}"
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
file(READ "${_such_index_snapshot_src}" _such_index_snapshot_text)
file(READ "${_such_index_policy_src}" _such_index_policy_text)
file(READ "${_such_process_control_src}" _such_process_control_text)
file(READ "${_such_index_worker_launcher}" _such_index_worker_launcher_text)
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
string(FIND "${_such_win_text}" "such::version::kWindowClassName" _versioned_class)
if(_versioned_class EQUAL -1)
  message(FATAL_ERROR "Win32 window class must use the generated version identity")
endif()
string(FIND "${_such_win_text}" "#include <such/Version.h>" _version_header)
if(_version_header EQUAL -1)
  message(FATAL_ERROR "Win32 product title must use the generated public Version.h")
endif()


# MSVC /W4 reports local-name reuse in chained if-init statements as C4456.
# Keep the portable compiler warning set aligned with this rule and reject the
# exact stale spellings that caused the v1.1 Windows build failure.
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

# Snapshot state-path discovery is compiled into the Windows core with /WX.
# Never call the CRT getenv directly in this file: MSVC reports C4996 and the
# warning is intentionally promoted to an error.
string(FIND "${_such_index_snapshot_text}" "std::getenv(\"SUCH_STATE_DIR\")" _unsafe_snapshot_getenv)
if(NOT _unsafe_snapshot_getenv EQUAL -1)
  message(FATAL_ERROR "Rejected MSVC C4996-prone std::getenv in IndexSnapshot")
endif()

# The test stub is also built with /WX. std::getenv is C4996 under MSVC, so
# test-only code must use the same secure Windows environment access discipline.
string(FIND "${_such_fake_runtime_text}" "std::getenv(\"SUCH_FAKE_RUNTIME_" _unsafe_test_getenv)
if(NOT _unsafe_test_getenv EQUAL -1)
  message(FATAL_ERROR "Rejected MSVC C4996-prone std::getenv in fake runtime")
endif()
string(FIND "${_such_fake_runtime_text}" "std::fopen(query_log" _unsafe_test_fopen)
if(NOT _unsafe_test_fopen EQUAL -1)
  message(FATAL_ERROR "Rejected MSVC C4996-prone std::fopen in fake runtime")
endif()


string(FIND "${_such_win_text}" "reinterpret_cast<WNDPROC>(SetWindowLongPtr" _unsafe_subclass_cast)
if(NOT _unsafe_subclass_cast EQUAL -1)
  message(FATAL_ERROR "Rejected C4191-prone Win32 WNDPROC subclass cast; use SetWindowSubclass")
endif()
string(FIND "${_such_win_text}" "SetWindowSubclass(" _subclass_helper)
if(_subclass_helper EQUAL -1)
  message(FATAL_ERROR "Win32 search EDIT must use SetWindowSubclass")
endif()

# Windows filesystem::path uses wchar_t. Never pass the wide comparable key to
# UTF-8 policy APIs: that compiles on POSIX only and fails on MSVC.
string(FIND "${_such_index_policy_text}" "is_windows_protected_index_path(path_key(" _wide_path_key_api)
if(NOT _wide_path_key_api EQUAL -1)
  message(FATAL_ERROR "Rejected Windows wide PathKey passed to UTF-8 policy API")
endif()

# EnumWindows requires a CALLBACK-compatible function pointer. Keep the explicit
# CALLBACK thunk instead of relying on a non-capturing lambda conversion.
string(FIND "${_such_process_control_text}" "BOOL CALLBACK close_such_window" _enum_callback)
if(_enum_callback EQUAL -1)
  message(FATAL_ERROR "Windows process control must use an explicit CALLBACK EnumWindows thunk")
endif()


# The detached worker command is ASCII by construction. Keep all widening and
# byte reinterpretation explicit so MSVC /W4 /WX cannot promote signed/width
# conversion diagnostics into build failures.
string(FIND "${_such_index_worker_launcher_text}" "for (const unsigned char byte : value)" _worker_signed_byte_loop)
if(NOT _worker_signed_byte_loop EQUAL -1)
  message(FATAL_ERROR "Rejected MSVC warning-prone implicit char-to-unsigned-char worker loop")
endif()
string(FIND "${_such_index_worker_launcher_text}" "std::wstring(value.begin(), value.end())" _worker_implicit_widen)
if(NOT _worker_implicit_widen EQUAL -1)
  message(FATAL_ERROR "Rejected MSVC warning-prone implicit char-to-wchar_t worker widening")
endif()

message(STATUS "Such Win32/MSVC known-hazard gate: PASS")
