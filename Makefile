# Convenience wrapper around the CMake presets.
SRC := $(shell find app drivers modem power startup tests/host -name '*.[ch]')

.PHONY: all fw fw-debug test format format-check lint flash clean

all: fw test

fw:
	cmake --preset fw-release && cmake --build --preset fw-release

fw-debug:
	cmake --preset fw-debug && cmake --build --preset fw-debug

test:
	cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests

format:
	clang-format -i $(SRC)

format-check:
	clang-format --dry-run --Werror $(SRC)

lint:
	cppcheck --enable=warning,style,performance,portability --std=c11 --inline-suppr --error-exitcode=1 \
		--suppress=missingIncludeSystem -DSTM32L476xx -D__GNUC__ -I drivers/include -I modem/include -I app/include -I power/include --library=gnu --quiet \
		--suppress="*:third_party/*" app drivers modem power startup

# Needs OpenOCD and a Nucleo board connected over ST-LINK.
flash: fw
	openocd -f interface/stlink.cfg -f target/stm32l4x.cfg \
		-c "program build/fw-release/tracker.elf verify reset exit"

clean:
	rm -rf build
