#
# Opencpn wrapper for FindLibarchive.cmake
#
# Export ocpn::libarchive transitive link object
#

if (TARGET ocpn::libarchive)
  return ()
endif ()

add_library(_archive_if INTERFACE)
add_library(ocpn::libarchive ALIAS _archive_if)

# On Android, use precompiled stuff and be done with it.
if (QT_ANDROID)
  set(_libarchive_root  ${CMAKE_SOURCE_DIR}/buildandroid/libarchive)
  set(_libarchive_lib "${_libarchive_root}/${ARCH}/libarchive.a")
  target_include_directories(
    _archive_if INTERFACE ${_libarchive_root}/include
  )
  target_link_libraries( _archive_if INTERFACE ${_libarchive_lib})
  if (NOT EXISTS ${_libarchive_lib})
    message(FATAL_ERROR "Required library ${_libarchive_lib} is missing")
  endif ()
  return ()
endif ()

if (APPLE)
  if (OCPN_USE_SYSTEM_LIBARCHIVE)
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/libarchive")
  else ()
    #  TODO This is a hack, due to the way libarchive is built and
    #  installed in CI build environment
    list(APPEND CMAKE_PREFIX_PATH "/opt/local")
  endif ()
endif ()

find_package(LibArchive REQUIRED)
target_include_directories(_archive_if INTERFACE ${LibArchive_INCLUDE_DIRS})
target_link_libraries(_archive_if INTERFACE ${LibArchive_LIBRARIES})
