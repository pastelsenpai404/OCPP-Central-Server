set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ui/index.html ui/styles.css ui/app.js)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ui/index.html" ADMIN_HTML)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ui/styles.css" ADMIN_CSS)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ui/app.js" ADMIN_JS)
# MSVC limits each string literal to 16 KiB. Keep UTF-8 assets as adjacent
# newline-sized literals, compiled into the executable without runtime files.
foreach(asset ADMIN_HTML ADMIN_CSS ADMIN_JS)
  string(REPLACE "\n" "\n)OCPPUI\"\nR\"OCPPUI(" ${asset} "${${asset}}")
endforeach()
configure_file(ui/assets.hpp.in generated/admin_assets.hpp @ONLY)
target_include_directories(ocpp_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
