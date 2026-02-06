cmake_minimum_required(VERSION 3.20)

project(cpp-data-structures)

# Set C++ standard
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add subdirectories
#add_subdirectory(hash-table)
#add_subdirectory(queues)
#add_subdirectory(arena-allocator)
# Use libc++ on macOS (optional, but matches your clang++ command)
if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif()

# Add executable
add_executable(arena-allocator arena-allocator/arena_alloc.cpp)


# Optional: Set output directory
# set_target_properties(${PROJECT_NAME} PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
# )