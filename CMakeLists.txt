cmake_minimum_required(VERSION 3.16)

project(LibrarySystem VERSION 0.1 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets GuiPrivate)

# QXlsx - MIT licensed Excel read/write library for Qt
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/QXlsx/QXlsx"
                 "${CMAKE_BINARY_DIR}/QXlsx"
                 EXCLUDE_FROM_ALL)

set(CORE_SOURCES
    src/Library.h
    src/Library.cpp
)

set(UI_SOURCES
    src/main.cpp
    src/CoverPreview.h
    src/MainWindow.h
    src/MainWindow.cpp
    src/MainWindow.ui
    src/BorrowBookForm.h
    src/BorrowBookForm.cpp
    src/BorrowBookForm.ui
    src/ReturnBookForm.h
    src/ReturnBookForm.cpp
    src/ReturnBookForm.ui
    src/QueryBookForm.h
    src/QueryBookForm.cpp
    src/QueryBookForm.ui
    src/RemindReturnForm.h
    src/RemindReturnForm.cpp
    src/RemindReturnForm.ui
    src/EditReaderForm.h
    src/EditReaderForm.cpp
    src/EditReaderForm.ui
    src/resources.qrc
)

execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_ID
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT GIT_COMMIT_ID)
    set(GIT_COMMIT_ID "unknown")
endif()
add_definitions(-DGIT_REVISION="${GIT_COMMIT_ID}")

if(WIN32)
    set(USER_NAME $ENV{USERNAME})
    set(HOST_NAME $ENV{COMPUTERNAME})
else()
    set(USER_NAME $ENV{USER})
    execute_process(COMMAND hostname OUTPUT_VARIABLE HOST_NAME OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
string(TIMESTAMP BUILD_DATE "%b %d %y %H:%M:%S")
add_definitions(-DBUILD_USER="${USER_NAME}")
add_definitions(-DBUILD_HOST="${HOST_NAME}")
add_definitions(-DBUILD_DATE="${BUILD_DATE}")

add_definitions(-DQT_MESSAGELOGCONTEXT)
qt_add_executable(LibrarySystem ${CORE_SOURCES} ${UI_SOURCES})

target_link_libraries(LibrarySystem PRIVATE
    Qt6::Widgets
    QXlsx::QXlsx
)

set_target_properties(LibrarySystem PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

include(GNUInstallDirs)
install(TARGETS LibrarySystem
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
