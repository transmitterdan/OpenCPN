cmake_minimum_required(VERSION 3.10.0)

# SQLite3 support required for MBTiles.

if (TARGET ocpn::sqlite_cpp)
    return ()
endif ()

set(SRC
    src/Backup.cpp
    src/Column.cpp
    src/Database.cpp
    src/Exception.cpp
    src/Savepoint.cpp
    src/Statement.cpp
    src/Transaction.cpp
)
set(INCL
    include/SQLiteCpp/SQLiteCpp.h
    include/SQLiteCpp/Assertion.h
    include/SQLiteCpp/Backup.h
    include/SQLiteCpp/Column.h
    include/SQLiteCpp/Database.h
    include/SQLiteCpp/Exception.h
    include/SQLiteCpp/ExecuteMany.h
    include/SQLiteCpp/Savepoint.h
    include/SQLiteCpp/SQLiteCppExport.h
    include/SQLiteCpp/Statement.h
    include/SQLiteCpp/Transaction.h
    include/SQLiteCpp/Utils.h
    include/SQLiteCpp/VariadicBind.h
)
add_library(SQLITE_CPP STATIC ${SRC} ${INCL})
add_library(ocpn::sqlite_cpp ALIAS SQLITE_CPP)

target_link_libraries(SQLITE_CPP PRIVATE ocpn::sqlite)

target_include_directories(SQLITE_CPP
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (TARGET LibLZMA::LibLZMA)
  target_link_libraries(SQLITE_CPP PUBLIC LibLZMA::LibLZMA)
endif ()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang|GNU")  # Apple is AppleClang
  target_compile_options(SQLITE_CPP PRIVATE -fvisibility=default -fPIC)
endif ()

# We do not need extensions, and using them in a static library causes
# trouble with libdl linking order.
if (UNIX AND NOT APPLE)
    add_definitions(" -DSQLITE_OMIT_LOAD_EXTENSION")
endif (UNIX AND NOT APPLE)
