add_executable(billing_domain_tests unit/quote_tests.cpp)
target_link_libraries(billing_domain_tests PRIVATE billing_domain billing_options)
add_test(NAME billing_domain COMMAND billing_domain_tests)
if(BILLING_BUILD_HTTP)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME billing_backoffice_workflows COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/backoffice_smoke.py --binary $<TARGET_FILE:billing_management_api>)
  set_tests_properties(billing_backoffice_workflows PROPERTIES TIMEOUT 90)
  add_test(NAME billing_registry_seed COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/registry_seed_smoke.py --binary $<TARGET_FILE:billing_management_api>)
  set_tests_properties(billing_registry_seed PROPERTIES TIMEOUT 90)
endif()
