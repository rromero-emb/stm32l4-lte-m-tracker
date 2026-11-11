add_library(unity STATIC ${CMAKE_SOURCE_DIR}/third_party/unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_SOURCE_DIR}/third_party/unity)

# Code under test, compiled natively. The stub core_cm4.h comes first in the include path.
add_library(fw_host STATIC
    ${CMAKE_SOURCE_DIR}/drivers/src/spi.c
    ${CMAKE_SOURCE_DIR}/drivers/src/ringbuf.c
    ${CMAKE_SOURCE_DIR}/drivers/src/fmt.c
    ${CMAKE_SOURCE_DIR}/drivers/src/fault_decode.c
)
target_compile_definitions(fw_host PUBLIC STM32L476xx SPI_TIMEOUT_LOOPS=1000U)
target_include_directories(fw_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/drivers/include
    ${CMAKE_SOURCE_DIR}/third_party/cmsis/device
)
target_compile_options(fw_host PRIVATE ${WARNINGS} -Werror --coverage -fsanitize=address,undefined)
target_link_options(fw_host PUBLIC --coverage -fsanitize=address,undefined)

foreach(t test_spi test_ringbuf test_fmt test_fault_decode test_uart_brr)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE fw_host unity)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
