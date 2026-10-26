cmake_minimum_required(VERSION 3.16)
project(VolleyballCareer26 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(vc26_core
    core/src/Types.cpp
    core/src/RoleFit.cpp
    core/src/RotationManager.cpp
    core/src/MatchScore.cpp
)
target_include_directories(vc26_core PUBLIC core/include)
if(MSVC)
    target_compile_options(vc26_core PRIVATE /W4)
else()
    target_compile_options(vc26_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(vc26_tests tests/test_main.cpp)
target_link_libraries(vc26_tests PRIVATE vc26_core)
add_test(NAME vc26_tests COMMAND vc26_tests)
