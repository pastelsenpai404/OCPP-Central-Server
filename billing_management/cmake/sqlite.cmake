enable_language(C)
FetchContent_Declare(billing_sqlite
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
  URL_HASH SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(billing_sqlite)
add_library(billing_sqlite STATIC ${billing_sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(billing_sqlite SYSTEM PUBLIC ${billing_sqlite_SOURCE_DIR})
target_compile_definitions(billing_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_DQS=0
  SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_FOREIGN_KEYS=1)
target_link_libraries(billing_sqlite PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
