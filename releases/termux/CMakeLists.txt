cmake_minimum_required(VERSION 3.10...3.31)
project(razor_engine CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

# Backwards compatibility helper for CMake < 3.14
macro(razor_fetchcontent_makeavailable NAME)
  if(COMMAND FetchContent_MakeAvailable)
    FetchContent_MakeAvailable(${NAME})
  else()
    FetchContent_GetProperties(${NAME})
    if(NOT ${NAME}_POPULATED)
      FetchContent_Populate(${NAME})
      add_subdirectory(${${NAME}_SOURCE_DIR} ${${NAME}_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
  endif()
endmacro()

find_package(ftxui CONFIG QUIET)
if (NOT ftxui_FOUND)
  FetchContent_Declare(ftxui
    URL https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v5.0.0.tar.gz
  )
  razor_fetchcontent_makeavailable(ftxui)
endif()

find_package(yaml-cpp CONFIG QUIET)
if (NOT yaml-cpp_FOUND)
  FetchContent_Declare(yaml-cpp
    URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
  )
  razor_fetchcontent_makeavailable(yaml-cpp)
endif()

find_package(nlohmann_json CONFIG QUIET)
if (NOT nlohmann_json_FOUND)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
  )
  razor_fetchcontent_makeavailable(json)
endif()

add_subdirectory(ui)
add_subdirectory(orchestrator)
add_subdirectory(router)

