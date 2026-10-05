.PHONY: all test build package
all:
	$(MAKE) build
	$(MAKE) package
test:
	@mkdir -p build/host
	c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. touch/core.cpp touch/draw.cpp common/config.cpp common/storage.cpp tests/core_tests.cpp -o build/host/core-tests
	./build/host/core-tests
	c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fakes -I. tests/bounded_client_tests.cpp -o build/host/bounded-client-tests
	./build/host/bounded-client-tests
	c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. tests/layer_stacks_tests.cpp -o build/host/layer-stacks-tests
	./build/host/layer-stacks-tests
	c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. touch/core.cpp tests/watchdog_tests.cpp -o build/host/watchdog-tests
	./build/host/watchdog-tests
	c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. touch/core.cpp touch/draw.cpp common/config.cpp common/storage.cpp tests/arcade_tests.cpp -o build/host/arcade-tests
	./build/host/arcade-tests
	c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. touch/core.cpp touch/draw.cpp tests/controls_tests.cpp -o build/host/controls-tests
	./build/host/controls-tests
	c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. tests/brightness_tests.cpp -o build/host/brightness-tests
	./build/host/brightness-tests
	python3 tests/run_brightness_slider_tests.py
build:
	python3 scripts/build.py
package:
	python3 scripts/package.py
