cmake_minimum_required(VERSION 3.16)

project(RNetMsgBroker VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core LinguistTools)

set(TS_FILES RNetMsgBroker_de_DE.ts)

add_library(RNetMsgBroker SHARED
  RNetMsgBroker_global.h
  rnetmsgbroker.cpp
  rnetmsgbroker.h
  ${TS_FILES}
)

target_include_directories(RNetMsgBroker
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(RNetMsgBroker PRIVATE Qt${QT_VERSION_MAJOR}::Core)

target_compile_definitions(RNetMsgBroker
  PRIVATE
    RNETMSGBROKER_LIBRARY
    RNETMSGBROKER_VERSION_STRING="${PROJECT_VERSION}"
)

if(COMMAND qt_create_translation)
    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

add_executable(RNetMsgBrokerTest
  testapp/main.cpp
)

target_link_libraries(RNetMsgBrokerTest PRIVATE RNetMsgBroker Qt${QT_VERSION_MAJOR}::Core)

add_custom_command(TARGET RNetMsgBrokerTest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/R-Net.json
          $<TARGET_FILE_DIR:RNetMsgBrokerTest>/R-Net.json
  COMMAND ${CMAKE_COMMAND} -E make_directory
          $<TARGET_FILE_DIR:RNetMsgBrokerTest>/testdata
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/testdata/candump_sample.txt
          $<TARGET_FILE_DIR:RNetMsgBrokerTest>/testdata/candump_sample.txt
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/testdata/candump_open_rnet_sample.txt
          $<TARGET_FILE_DIR:RNetMsgBrokerTest>/testdata/candump_open_rnet_sample.txt
)


# Optional install target for packaging or downstream CMake projects.
include(GNUInstallDirs)
install(TARGETS RNetMsgBroker
  EXPORT RNetMsgBrokerTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES rnetmsgbroker.h RNetMsgBroker_global.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/RNetMsgBroker
)
install(FILES R-Net.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/RNetMsgBroker
)

# Delivery archive target.
#
# Usage:
#   cmake --build build --target deliver
#
# The archive is a source-delivery ZIP for GitHub/review handoff.  It is
# intentionally built from an explicit file list so that build directories,
# generated CSV/log files and a local .git directory cannot slip into the ZIP.
get_filename_component(RNET_DELIVER_DIRNAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
set(RNET_DELIVER_ARCHIVE "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_v${PROJECT_VERSION}.zip")
set(RNET_DELIVER_FILES
  "${RNET_DELIVER_DIRNAME}/.gitignore"
  "${RNET_DELIVER_DIRNAME}/CMakeLists.txt"
  "${RNET_DELIVER_DIRNAME}/R-Net.json"
  "${RNET_DELIVER_DIRNAME}/README.md"
  "${RNET_DELIVER_DIRNAME}/CHANGELOG.md"
  "${RNET_DELIVER_DIRNAME}/AUTHORS.md"
  "${RNET_DELIVER_DIRNAME}/CONTRIBUTING.md"
  "${RNET_DELIVER_DIRNAME}/SECURITY.md"
  "${RNET_DELIVER_DIRNAME}/LICENSE_TODO.md"
  "${RNET_DELIVER_DIRNAME}/VERSION.txt"
  "${RNET_DELIVER_DIRNAME}/RNetMsgBroker_global.h"
  "${RNET_DELIVER_DIRNAME}/rnetmsgbroker.h"
  "${RNET_DELIVER_DIRNAME}/rnetmsgbroker.cpp"
  "${RNET_DELIVER_DIRNAME}/RNetMsgBroker_de_DE.ts"
  "${RNET_DELIVER_DIRNAME}/testapp/main.cpp"
  "${RNET_DELIVER_DIRNAME}/testdata/candump_sample.txt"
  "${RNET_DELIVER_DIRNAME}/testdata/candump_open_rnet_sample.txt"
  "${RNET_DELIVER_DIRNAME}/scripts/build.sh"
  "${RNET_DELIVER_DIRNAME}/scripts/check.sh"
  "${RNET_DELIVER_DIRNAME}/scripts/candump_to_csv.sh"
  "${RNET_DELIVER_DIRNAME}/scripts/list_unknown_observed.py"
  "${RNET_DELIVER_DIRNAME}/docs/CSV_FORMAT.md"
  "${RNET_DELIVER_DIRNAME}/docs/RNET_JSON_SCHEMA.md"
  "${RNET_DELIVER_DIRNAME}/docs/STARTUP_SEQUENCE.md"
  "${RNET_DELIVER_DIRNAME}/docs/UNKNOWN_FRAMES.md"
  "${RNET_DELIVER_DIRNAME}/.github/workflows/cmake.yml"
  "${RNET_DELIVER_DIRNAME}/.github/ISSUE_TEMPLATE/bug_report.yml"
  "${RNET_DELIVER_DIRNAME}/.github/ISSUE_TEMPLATE/config.yml"
  "${RNET_DELIVER_DIRNAME}/.github/pull_request_template.md"
)

add_custom_target(deliver
  COMMAND ${CMAKE_COMMAND} -E rm -f "${RNET_DELIVER_ARCHIVE}"
  COMMAND ${CMAKE_COMMAND} -E tar "cfv" "${RNET_DELIVER_ARCHIVE}" --format=zip ${RNET_DELIVER_FILES}
  COMMAND ${CMAKE_COMMAND} -E echo "Delivery archive written to: ${RNET_DELIVER_ARCHIVE}"
  DEPENDS RNetMsgBroker RNetMsgBrokerTest
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
  COMMENT "Creating delivery archive ${RNET_DELIVER_ARCHIVE}"
  VERBATIM
)
