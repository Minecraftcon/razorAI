add_library(razor_ui_lib STATIC
  razor_ui.cpp
  c_bridge.cpp
)

target_include_directories(razor_ui_lib PUBLIC 
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/router/include
  ${CMAKE_SOURCE_DIR}/orchestrator/include
)

target_link_libraries(razor_ui_lib PUBLIC
  ftxui::screen
  ftxui::dom
  ftxui::component
  m
)

add_executable(razor_cpp_standalone
  standalone_main.cpp
)

target_include_directories(razor_cpp_standalone PRIVATE
  ${CMAKE_SOURCE_DIR}/router/include
  ${CMAKE_SOURCE_DIR}/orchestrator/include
)

target_link_libraries(razor_cpp_standalone PRIVATE
  razor_ui_lib
  razor_router_lib
  razor_config_lib
  pthread
  ${CURL_LIBRARIES}
  nlohmann_json::nlohmann_json
  yaml-cpp
)
