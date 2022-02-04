################################################################################
##  TESTS  #####################################################################
################################################################################

# Thread library
find_package(Threads)
# DPDK library
find_package(Dpdk REQUIRED)

include(FetchContent)
FetchContent_Declare(
    docopt
    GIT_REPOSITORY https://github.com/docopt/docopt.cpp.git
    GIT_TAG        3dd23e3280f213bacefdf5fcb04857bf52e90917
)

FetchContent_GetProperties(docopt)
if(NOT docopt_POPULATED)
    FetchContent_Populate(docopt)
    add_subdirectory(
        ${docopt_SOURCE_DIR}
        ${docopt_BINARY_DIR}
        EXCLUDE_FROM_ALL
    )
endif()

## system_test #################################################################

add_executable(system_test
    system_test.cc
)
target_link_libraries(system_test
    Homa
    FakeDriver
    Threads::Threads
    docopt
)

## dpdk_test #################################################################

add_executable(dpdk_test
    dpdk_test.cc
)
target_link_libraries(dpdk_test
    PRIVATE
        Homa::DpdkDriver
        docopt
        PerfUtils
)


## dpdk_test_client #################################################################

add_executable(dpdk_test_client
    dpdk_test_client.cc
)
target_link_libraries(dpdk_test_client
    PRIVATE
        Homa::DpdkDriver
        docopt
        PerfUtils
)

## dpdk_client_system_test #####################################################

add_executable(dpdk_client_system_test
    dpdk_client_system_test.cc
)
target_link_libraries(dpdk_client_system_test
    PRIVATE
        Homa::DpdkDriver
        docopt
        PerfUtils
        rt
)

## dpdk_server_system_test #####################################################

add_executable(dpdk_server_system_test
    dpdk_server_system_test.cc
    ./openloop/Generator.cc
    ./openloop/util.cc
    ./openloop/log.cc
)
target_link_libraries(dpdk_server_system_test
    PRIVATE
        Homa::DpdkDriver
        docopt
        PerfUtils
)

## dpdk_openloop_test #########################################################
add_executable(dpdk_openloop_test
    ./openloop/dpdk_openloop_test.cc
    ./openloop/Generator.cc
    ./openloop/util.cc
    ./openloop/log.cc
)
target_link_libraries(dpdk_openloop_test
    PRIVATE
        Dpdk::Dpdk
        Homa::DpdkDriver
        docopt
        PerfUtils
        rt
)

## Perf ########################################################################

add_executable(Perf
    Perf.cc
)
target_include_directories(Perf
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(Perf
    docopt
    PerfUtils
    Homa
)
