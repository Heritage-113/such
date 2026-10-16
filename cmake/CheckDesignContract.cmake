# Authoritative UI/UX contract checks used during configure.
#
# Do not byte-hash text documentation here. Git checkouts may represent the
# same text with LF or CRLF line endings, especially on Windows. Exact/canonical
# Design.md integrity belongs to the repository audits, which normalize line
# endings before hashing. Configure is responsible for build-relevant semantic
# checks only.
set(_such_design_file "${CMAKE_CURRENT_LIST_DIR}/../docs/Design.md")
if(NOT EXISTS "${_such_design_file}")
  message(FATAL_ERROR "Such design authority missing: docs/Design.md")
endif()

# The flat placeholder UI from the failed handoff must never re-enter a platform shell.
foreach(_src
    "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/Win32Frontend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../platform/linux/X11Frontend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../platform/ipados/IPadFrontend.mm")
  if(EXISTS "${_src}")
    file(READ "${_src}" _text)
    string(FIND "${_text}" "Result 1  [Location | Index | Pin]" _bad_flat_row)
    if(NOT _bad_flat_row EQUAL -1)
      message(FATAL_ERROR "Rejected legacy flat placeholder UI in ${_src}")
    endif()
    string(FIND "${_text}" "Search anything" _bad_old_placeholder)
    if(NOT _bad_old_placeholder EQUAL -1)
      message(FATAL_ERROR "Rejected old Search anything placeholder in ${_src}; v1.1.0 uses 2026 Heritage Inc.")
    endif()
    string(TOLOWER "${_text}" _text_lower)
    string(FIND "${_text_lower}" "footer" _bad_footer)
    if(NOT _bad_footer EQUAL -1)
      message(FATAL_ERROR "Rejected persistent footer UI in ${_src}; v1.1.0 keeps branding only in the empty search placeholder.")
    endif()
  endif()
endforeach()

message(STATUS "Such design authority: present; semantic frontend checks PASS")

# Canonical app-icon assets must ship with the SourceOnly tree.
foreach(_icon
    "${CMAKE_CURRENT_LIST_DIR}/../assets/icon/SuchLogoOriginal.png"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/icon/SuchLogoOriginal.ai"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/icon/SuchIcon_1024.png"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/icon/Such.ico")
  if(NOT EXISTS "${_icon}")
    message(FATAL_ERROR "Such canonical app icon asset missing: ${_icon}")
  endif()
endforeach()

# Canonical uploaded artwork is binary and therefore safe to verify byte-for-byte.
# Derived platform assets may be regenerated, but these source files must remain
# identical.
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../assets/icon/SuchLogoOriginal.png" _such_logo_png_sha)
if(NOT _such_logo_png_sha STREQUAL "b0d82ed475c96930fc018bad7931a8c25f5a7f8ec1e320268fe7aac88cc01b4d")
  message(FATAL_ERROR "Canonical SuchLogoOriginal.png changed unexpectedly: ${_such_logo_png_sha}")
endif()
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../assets/icon/SuchLogoOriginal.ai" _such_logo_ai_sha)
if(NOT _such_logo_ai_sha STREQUAL "15c0b945315e13abe542e125a83e81503d126b39e6b3a0dc8a3a671e8bbe502f")
  message(FATAL_ERROR "Canonical SuchLogoOriginal.ai changed unexpectedly: ${_such_logo_ai_sha}")
endif()


# Root/indexing architecture ownership. Root selection must never regress into a
# synchronous frontend or RuntimeClient filesystem crawl. GUI frontends are
# read/search clients only for crawl-producing operations; root mutation and
# reindexing execute in the sibling SuchCLI worker process. IndexRootService
# remains the application-service owner inside that worker.
set(_such_runtime_client "${CMAKE_CURRENT_LIST_DIR}/../src/runtime/RuntimeClient.cpp")
set(_such_root_service "${CMAKE_CURRENT_LIST_DIR}/../src/runtime/IndexRootService.cpp")
foreach(_required_source "${_such_runtime_client}" "${_such_root_service}")
  if(NOT EXISTS "${_required_source}")
    message(FATAL_ERROR "Such root architecture source missing: ${_required_source}")
  endif()
endforeach()

file(READ "${_such_runtime_client}" _such_runtime_client_text)
foreach(_forbidden_runtime_token "plan_default_index_policy" "recursive_directory_iterator")
  string(FIND "${_such_runtime_client_text}" "${_forbidden_runtime_token}" _runtime_policy_leak)
  if(NOT _runtime_policy_leak EQUAL -1)
    message(FATAL_ERROR "RuntimeClient must remain a raw ABI adapter; root policy leaked back in: ${_forbidden_runtime_token}")
  endif()
endforeach()

file(READ "${_such_root_service}" _such_root_service_text)
string(FIND "${_such_root_service_text}" "plan_default_index_policy_set(policy_requests, false" _bounded_policy_call)
if(_bounded_policy_call EQUAL -1)
  message(FATAL_ERROR "IndexRootService must compile only bounded root-set policy on the caller path.")
endif()
string(FIND "${_such_root_service_text}" "runtime.replace_roots({}, &mutation_error)" _policy_reset_order)
string(FIND "${_such_root_service_text}" "apply_exclusions(runtime, prepared.exclusions" _policy_seed_order)
if(_policy_reset_order EQUAL -1 OR _policy_seed_order EQUAL -1 OR _policy_seed_order LESS _policy_reset_order)
  message(FATAL_ERROR "IndexRootService must reset runtime roots before seeding exclusions for /drive.")
endif()
string(FIND "${_such_root_service_text}" "recursive_directory_iterator" _root_service_recursive_walk)
if(NOT _root_service_recursive_walk EQUAL -1)
  message(FATAL_ERROR "IndexRootService must not recursively walk a selected storage root on the caller thread.")
endif()

set(_such_worker_launcher "${CMAKE_CURRENT_LIST_DIR}/../src/runtime/IndexWorkerLauncher.cpp")
set(_such_cli_app "${CMAKE_CURRENT_LIST_DIR}/../src/cli/CliApp.cpp")
foreach(_required_worker_source "${_such_worker_launcher}" "${_such_cli_app}")
  if(NOT EXISTS "${_required_worker_source}")
    message(FATAL_ERROR "Such external index-worker source missing: ${_required_worker_source}")
  endif()
endforeach()
file(READ "${_such_worker_launcher}" _such_worker_launcher_text)
file(READ "${_such_cli_app}" _such_cli_app_text)
string(FIND "${_such_worker_launcher_text}" "--index-worker" _worker_protocol_marker)
string(FIND "${_such_cli_app_text}" "run_index_worker" _worker_execution_marker)
if(_worker_protocol_marker EQUAL -1 OR _worker_execution_marker EQUAL -1)
  message(FATAL_ERROR "Such crawl-producing operations require the sibling SuchCLI index worker protocol.")
endif()

foreach(_frontend_source
    "${CMAKE_CURRENT_LIST_DIR}/../platform/windows/Win32Frontend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../platform/linux/X11Frontend.cpp")
  if(EXISTS "${_frontend_source}")
    file(READ "${_frontend_source}" _frontend_root_text)
    foreach(_forbidden_frontend_token
        "plan_default_index_policy"
        "recursive_directory_iterator"
        "IndexRootService"
        "reindex_async("
        "replace_roots("
        "add_root(")
      string(FIND "${_frontend_root_text}" "${_forbidden_frontend_token}" _frontend_policy_leak)
      if(NOT _frontend_policy_leak EQUAL -1)
        message(FATAL_ERROR "GUI frontend must not perform crawl-producing root/index work in-process: ${_frontend_source} -> ${_forbidden_frontend_token}")
      endif()
    endforeach()
    string(FIND "${_frontend_root_text}" "launch_index_worker" _frontend_worker_delegate)
    if(_frontend_worker_delegate EQUAL -1)
      message(FATAL_ERROR "GUI frontend must delegate crawl-producing work to the sibling index worker: ${_frontend_source}")
    endif()
  endif()
endforeach()

message(STATUS "Such root/index architecture: external-worker ownership checks PASS")
