CMAKE = /home/linuxbrew/.linuxbrew/bin/cmake

.PHONY: all build clean run

all: build

build:
	mkdir -p build
	$(CMAKE) -S . -B build
	$(CMAKE) --build build --target bronx

clean:
	rm -rf build

run: build
	cd build && ./bronx
