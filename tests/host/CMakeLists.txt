add_library(unity STATIC ${CMAKE_SOURCE_DIR}/third_party/unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_SOURCE_DIR}/third_party/unity)

# Code under test, compiled natively. The stub core_cm4.h comes first in the include path.
add_library(fw_host STATIC
    ${CMAKE_SOURCE_DIR}/drivers/src/spi.c
    ${CMAKE_SOURCE_DIR}/drivers/src/ringbuf.c
    ${CMAKE_SOURCE_DIR}/drivers/src/fmt.c
    ${CMAKE_SOURCE_DIR}/drivers/src/fault_decode.c
    ${CMAKE_SOURCE_DIR}/modem/src/at.c
    ${CMAKE_SOURCE_DIR}/modem/src/bg95.c
    ${CMAKE_SOURCE_DIR}/app/src/payload.c
)
target_compile_definitions(fw_host PUBLIC STM32L476xx SPI_TIMEOUT_LOOPS=1000U)
target_include_directories(fw_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/drivers/include
    ${CMAKE_SOURCE_DIR}/modem/include
    ${CMAKE_SOURCE_DIR}/app/include
    ${CMAKE_SOURCE_DIR}/third_party/cmsis/device
)
target_compile_options(fw_host PRIVATE ${WARNINGS} -Werror --coverage -fsanitize=address,undefined)
target_link_options(fw_host PUBLIC --coverage -fsanitize=address,undefined)

set(HOST_TESTS test_spi test_ringbuf test_fmt test_fault_decode test_uart_brr test_at test_bg95 test_payload)
foreach(t ${HOST_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE fw_host unity)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
target_sources(test_bg95 PRIVATE fake_modem.c)

# C codec vs Python reference decoder vs shared vectors
add_executable(payload_cli payload_cli.c)
target_link_libraries(payload_cli PRIVATE fw_host)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME payload_vectors
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_payload_vectors.py $<TARGET_FILE:payload_cli> ${CMAKE_SOURCE_DIR})
