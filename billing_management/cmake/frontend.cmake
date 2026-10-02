function(billing_frontend target)
  add_executable(${target} ${PROJECT_SOURCE_DIR}/shared/frontend/wasm/quote_exports.cpp)
  target_link_libraries(${target} PRIVATE billing_domain billing_options)
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME billing SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dist")
  target_link_options(${target} PRIVATE
    --no-entry
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sENVIRONMENT=web"
    "-sFILESYSTEM=0"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXPORTED_FUNCTIONS=['_billing_preview_satang']")
endfunction()
