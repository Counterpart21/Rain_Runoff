cmake_minimum_required(VERSION 3.24)
project(RainRunoffAssetTools LANGUAGES CXX)
include(FetchContent)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nifly
  GIT_REPOSITORY https://github.com/ousnius/nifly.git
  GIT_TAG 40584be62399c64a3cb54fd2a68466e739932f39)
FetchContent_MakeAvailable(nifly)
add_executable(runoff_assets generate_mesh.cpp)
target_link_libraries(runoff_assets PRIVATE nifly)
target_compile_features(runoff_assets PRIVATE cxx_std_17)
