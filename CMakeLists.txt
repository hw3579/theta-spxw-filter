cmake_minimum_required(VERSION 3.20)
project(theta_spxw_filter LANGUAGES CXX)

option(BUILD_TESTING "Build filter tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(theta_filter_core STATIC
    src/core.cpp
    src/protobuf.cpp
)
target_include_directories(theta_filter_core PUBLIC include)
target_compile_definitions(theta_filter_core PUBLIC
    BOOST_ERROR_CODE_HEADER_ONLY
    BOOST_SYSTEM_NO_DEPRECATED
)
target_compile_options(theta_filter_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(theta_filter_core PUBLIC Threads::Threads)

add_executable(theta-spxw-filter src/main.cpp)
target_link_libraries(theta-spxw-filter PRIVATE theta_filter_core Threads::Threads)
target_compile_options(theta-spxw-filter PRIVATE -Wall -Wextra -Wpedantic)
# The deployment target is Debian 12. Build a fully static runtime so the
# artifact does not inherit the build host's newer glibc symbol versions.
target_link_options(theta-spxw-filter PRIVATE -static -static-libstdc++ -static-libgcc)

if(BUILD_TESTING)
    include(CTest)
    find_package(Protobuf REQUIRED)

    set(THETA_EVENTS_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/proto/theta_events.proto)
    protobuf_generate_cpp(THETA_EVENTS_PROTO_SRCS THETA_EVENTS_PROTO_HDRS ${THETA_EVENTS_PROTO})

    add_executable(theta_filter_core_test tests/core_test.cpp)
    target_link_libraries(theta_filter_core_test PRIVATE theta_filter_core)
    target_compile_options(theta_filter_core_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME theta_filter_core_test COMMAND theta_filter_core_test)

    add_executable(theta_filter_protobuf_test
        tests/protobuf_test.cpp
        ${THETA_EVENTS_PROTO_SRCS}
        ${THETA_EVENTS_PROTO_HDRS}
    )
    target_include_directories(theta_filter_protobuf_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(theta_filter_protobuf_test PRIVATE theta_filter_core protobuf::libprotobuf)
    target_compile_options(theta_filter_protobuf_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME theta_filter_protobuf_test COMMAND theta_filter_protobuf_test)
endif()
