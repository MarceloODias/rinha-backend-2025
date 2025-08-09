# Install script for directory: /Users/admin/Projects/rinha/payment-middleware/restbed

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/Users/admin/Projects/rinha/payment-middleware/restbed/distribution")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/restbed")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include" TYPE FILE FILES "/Users/admin/Projects/rinha/payment-middleware/restbed/source/restbed")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/byte.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/common.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/context_placeholder.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/context_placeholder_base.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/context_value.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/http.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/logger.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/request.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/resource.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/response.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/rule.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/service.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/session.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/session_manager.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/settings.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/ssl_settings.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/status_code.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/string.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/uri.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/web_socket.hpp;/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed/web_socket_message.hpp")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/Users/admin/Projects/rinha/payment-middleware/restbed/distribution/include/corvusoft/restbed" TYPE FILE FILES
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/byte.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/common.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/context_placeholder.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/context_placeholder_base.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/context_value.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/http.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/logger.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/request.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/resource.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/response.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/rule.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/service.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/session.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/session_manager.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/settings.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/ssl_settings.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/status_code.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/string.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/uri.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/web_socket.hpp"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/source/corvusoft/restbed/web_socket_message.hpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "library" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/library" TYPE STATIC_LIBRARY FILES "/Users/admin/Projects/rinha/payment-middleware/restbed/build/librestbed.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/library/librestbed.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/library/librestbed.a")
    execute_process(COMMAND "/usr/bin/ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/library/librestbed.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/library" TYPE SHARED_LIBRARY FILES
    "/Users/admin/Projects/rinha/payment-middleware/restbed/build/librestbed.4.8.dylib"
    "/Users/admin/Projects/rinha/payment-middleware/restbed/build/librestbed.4.dylib"
    )
  foreach(file
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/library/librestbed.4.8.dylib"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/library/librestbed.4.dylib"
      )
    if(EXISTS "${file}" AND
       NOT IS_SYMLINK "${file}")
      if(CMAKE_INSTALL_DO_STRIP)
        execute_process(COMMAND "/usr/bin/strip" -x "${file}")
      endif()
    endif()
  endforeach()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/library" TYPE SHARED_LIBRARY FILES "/Users/admin/Projects/rinha/payment-middleware/restbed/build/librestbed.dylib")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/admin/Projects/rinha/payment-middleware/restbed/build/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/admin/Projects/rinha/payment-middleware/restbed/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
