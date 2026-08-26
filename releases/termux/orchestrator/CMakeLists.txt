include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(razor_config_lib STATIC
  src/config.cpp
)

target_include_directories(razor_config_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(razor_config_lib PUBLIC yaml-cpp)

add_executable(razor_orchestrator
  src/main.cpp
  src/process_runner.cpp
  src/orchestrator.cpp
)

target_link_libraries(razor_orchestrator PRIVATE
  razor_config_lib
  razor_ui_lib
  razor_router_lib
  pthread
)
