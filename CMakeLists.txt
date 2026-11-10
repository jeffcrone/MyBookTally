# MyBookTally - top-level build script.
#
# The version below is the single source of truth for the app's version. The
# app's --version output, its About box, the Windows file properties, and the
# GitHub release workflow (which reads this line to decide whether a new
# release is due) all derive from it. Bump it here and nowhere else.
#
# 3.25 is needed for the "workflow" presets in CMakePresets.json, which let a
# single command configure, build, and test.
cmake_minimum_required(VERSION 3.25)

project(MyBookTally
	VERSION 0.0.1
	DESCRIPTION "Keep track of the books in your personal collection"
	HOMEPAGE_URL "https://github.com/jeffcrone/MyBookTally"
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6.4 is the oldest release this is built and tested against (it is what
# Ubuntu 24.04 ships). The Windows release build uses a newer Qt 6 LTS.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Test)
qt_standard_project_setup()

option(MYBOOKTALLY_BUILD_TESTS "Build the automated tests" ON)

add_subdirectory(src/core)
add_subdirectory(src/app)

if(MYBOOKTALLY_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
