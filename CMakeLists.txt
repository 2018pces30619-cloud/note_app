cmake_minimum_required(VERSION 3.25)

project(BranchNotes
    VERSION 0.1.0
    DESCRIPTION "A local-first Windows notes application"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(BranchNotes WIN32
    src/main.cpp
    src/platform/win32/main_window.cpp
    src/storage/local_note_store.cpp
)
set_target_properties(BranchNotes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/deliverable"
    OUTPUT_NAME "BranchNotes-Map-Exact-Clip"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_include_directories(BranchNotes PRIVATE include)
target_compile_definitions(BranchNotes PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(BranchNotes PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

target_link_libraries(BranchNotes PRIVATE comctl32 comdlg32 shell32 ole32)
