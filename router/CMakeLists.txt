include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/orchestrator/include)

find_package(CURL REQUIRED)

# Static router library
add_library(razor_router_lib STATIC
  src/context_cache.cpp
  src/embedding_classifier.cpp
  src/router.cpp
  src/c_router_bridge.cpp
  src/session_manager.cpp
  src/socket_daemon.cpp
  src/http_client.cpp
  src/task_manager.cpp
  src/file_inspector.cpp
  src/skill_manager.cpp
  src/web_search.cpp
)

# Shared router library for Go CGO
add_library(razor_router_shared SHARED
  src/context_cache.cpp
  src/embedding_classifier.cpp
  src/router.cpp
  src/c_router_bridge.cpp
  src/session_manager.cpp
  src/socket_daemon.cpp
  src/http_client.cpp
  src/task_manager.cpp
  src/file_inspector.cpp
  src/skill_manager.cpp
  src/web_search.cpp
)

target_include_directories(razor_router_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/orchestrator/include ${CURL_INCLUDE_DIRS})
target_include_directories(razor_router_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/orchestrator/include ${CURL_INCLUDE_DIRS})

target_link_libraries(razor_router_lib PUBLIC ${CURL_LIBRARIES} nlohmann_json::nlohmann_json razor_config_lib yaml-cpp)
target_link_libraries(razor_router_shared PUBLIC ${CURL_LIBRARIES} nlohmann_json::nlohmann_json razor_config_lib yaml-cpp)

# Router socket daemon executable
add_executable(razor_router_daemon
  src/main_daemon.cpp
)

target_link_libraries(razor_router_daemon PRIVATE razor_router_lib razor_config_lib pthread ${CURL_LIBRARIES})

# Unit test executable
add_executable(test_router
  src/test_router.cpp
)

target_link_libraries(test_router PRIVATE razor_router_lib razor_config_lib pthread ${CURL_LIBRARIES})
