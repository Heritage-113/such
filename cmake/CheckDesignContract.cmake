# Authoritative UI/UX contract integrity gate.
# If docs/Design.md is intentionally revised, update the expected hash in the same change
# and re-audit all frontend contract tests.
set(_such_design_file "${CMAKE_CURRENT_LIST_DIR}/../docs/Design.md")
if(NOT EXISTS "${_such_design_file}")
  message(FATAL_ERROR "Such design authority missing: docs/Design.md")
endif()
file(SHA256 "${_such_design_file}" _such_design_sha256)
set(_such_design_expected "2c17b7338efec9351eba507f17cddbca3319829d7684dc599a5078f170780913")
if(NOT _such_design_sha256 STREQUAL _such_design_expected)
  message(FATAL_ERROR
    "docs/Design.md changed without updating the authoritative design gate.\n"
    "Expected: ${_such_design_expected}\nActual:   ${_such_design_sha256}")
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
      message(FATAL_ERROR "Rejected old Search anything placeholder in ${_src}; v1.0.0 uses 2026 Heritage Inc.")
    endif()
    string(TOLOWER "${_text}" _text_lower)
    string(FIND "${_text_lower}" "footer" _bad_footer)
    if(NOT _bad_footer EQUAL -1)
      message(FATAL_ERROR "Rejected persistent footer UI in ${_src}; v1.0.0 keeps branding only in the empty search placeholder.")
    endif()
  endif()
endforeach()

message(STATUS "Such authoritative Design.md: PASS (${_such_design_sha256})")

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

# The canonical uploaded artwork is immutable. Derived platform assets may be
# regenerated, but the two source files must remain byte-for-byte identical.
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../assets/icon/SuchLogoOriginal.png" _such_logo_png_sha)
if(NOT _such_logo_png_sha STREQUAL "b0d82ed475c96930fc018bad7931a8c25f5a7f8ec1e320268fe7aac88cc01b4d")
  message(FATAL_ERROR "Canonical SuchLogoOriginal.png changed unexpectedly: ${_such_logo_png_sha}")
endif()
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../assets/icon/SuchLogoOriginal.ai" _such_logo_ai_sha)
if(NOT _such_logo_ai_sha STREQUAL "15c0b945315e13abe542e125a83e81503d126b39e6b3a0dc8a3a671e8bbe502f")
  message(FATAL_ERROR "Canonical SuchLogoOriginal.ai changed unexpectedly: ${_such_logo_ai_sha}")
endif()
