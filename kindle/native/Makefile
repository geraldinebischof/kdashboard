CXX ?= g++
KINDLE_CXX ?= arm-linux-gnueabi-g++
ZIG ?= zig
ZIG_TARGET ?= arm-linux-musleabi
ZIG_MCPU ?=
ZIG_CPU_FLAG := $(if $(ZIG_MCPU),-mcpu=$(ZIG_MCPU),)
ZIG_LDFLAGS ?= -static

SRC := src/kindle_dashboard.cpp
SRCS := $(wildcard src/*.cpp)
HDRS := $(wildcard src/*.h)
BUILD_DIR := build
TARGET := $(BUILD_DIR)/kindle-dashboard-local
KINDLE_TARGET := $(BUILD_DIR)/kindle-dashboard-arm
ZIG_TARGET_BINARY := $(BUILD_DIR)/kindle-dashboard-zig
EXTENSION_ROOT := $(BUILD_DIR)/kual-package
EXTENSION_DIR := $(EXTENSION_ROOT)/kindle-dashboard
EXTENSION_ARCHIVE := $(BUILD_DIR)/kindle-dashboard-kual.tar.gz

CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2

.PHONY: all local kindle kindle-zig extension extension-zig check clean

all: local

local: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

kindle: $(KINDLE_TARGET)

$(KINDLE_TARGET): $(SRCS) $(HDRS)
	mkdir -p $(BUILD_DIR)
	@command -v $(KINDLE_CXX) >/dev/null 2>&1 || { \
		echo "Missing Kindle cross compiler: $(KINDLE_CXX)"; \
		echo "Install a compatible ARM toolchain or rerun with KINDLE_CXX=/path/to/compiler"; \
		exit 1; \
	}
	$(KINDLE_CXX) $(CXXFLAGS) -static -o $@ $(SRCS)

kindle-zig: $(ZIG_TARGET_BINARY)

$(ZIG_TARGET_BINARY): $(SRCS) $(HDRS)
	mkdir -p $(BUILD_DIR)
	@command -v $(ZIG) >/dev/null 2>&1 || { \
		echo "Missing Zig compiler: $(ZIG)"; \
		echo "Install Zig or rerun with ZIG=/path/to/zig"; \
		exit 1; \
	}
	$(ZIG) c++ -target $(ZIG_TARGET) $(ZIG_CPU_FLAG) $(CXXFLAGS) $(ZIG_LDFLAGS) -s -o $@ $(SRCS)

extension: $(KINDLE_TARGET)
	rm -rf $(EXTENSION_ROOT)
	mkdir -p $(EXTENSION_DIR)/bin
	mkdir -p $(EXTENSION_DIR)/assets
	cp ../kual/kindle-dashboard/config.xml $(EXTENSION_DIR)/config.xml
	cp ../kual/kindle-dashboard/config.sh.example $(EXTENSION_DIR)/config.sh.example
	cp ../kual/kindle-dashboard/menu.json $(EXTENSION_DIR)/menu.json
	if [ -d ../kual/kindle-dashboard/assets/recipes ]; then cp -R ../kual/kindle-dashboard/assets/recipes $(EXTENSION_DIR)/assets/recipes; fi
	cp ../kual/kindle-dashboard/assets/cookbook-icon.pgm $(EXTENSION_DIR)/assets/cookbook-icon.pgm
	cp ../kual/kindle-dashboard/bin/menu-ping.sh $(EXTENSION_DIR)/bin/menu-ping.sh
	cp ../kual/kindle-dashboard/bin/start.sh $(EXTENSION_DIR)/bin/start.sh
	cp ../kual/kindle-dashboard/bin/start-light.sh $(EXTENSION_DIR)/bin/start-light.sh
	cp ../kual/kindle-dashboard/bin/start-dark.sh $(EXTENSION_DIR)/bin/start-dark.sh
	cp ../kual/kindle-dashboard/bin/once.sh $(EXTENSION_DIR)/bin/once.sh
	cp ../kual/kindle-dashboard/bin/once-light.sh $(EXTENSION_DIR)/bin/once-light.sh
	cp ../kual/kindle-dashboard/bin/once-dark.sh $(EXTENSION_DIR)/bin/once-dark.sh
	cp ../kual/kindle-dashboard/bin/proof.sh $(EXTENSION_DIR)/bin/proof.sh
	cp ../kual/kindle-dashboard/bin/stop.sh $(EXTENSION_DIR)/bin/stop.sh
	cp ../kual/kindle-dashboard/bin/dashboard.sh $(EXTENSION_DIR)/bin/dashboard.sh
	cp ../kual/kindle-dashboard/bin/diagnose.sh $(EXTENSION_DIR)/bin/diagnose.sh
	cp $(KINDLE_TARGET) $(EXTENSION_DIR)/bin/kindle-dashboard
	chmod +x $(EXTENSION_DIR)/bin/menu-ping.sh $(EXTENSION_DIR)/bin/start.sh $(EXTENSION_DIR)/bin/start-light.sh $(EXTENSION_DIR)/bin/start-dark.sh $(EXTENSION_DIR)/bin/once.sh $(EXTENSION_DIR)/bin/once-light.sh $(EXTENSION_DIR)/bin/once-dark.sh $(EXTENSION_DIR)/bin/proof.sh $(EXTENSION_DIR)/bin/stop.sh $(EXTENSION_DIR)/bin/dashboard.sh $(EXTENSION_DIR)/bin/diagnose.sh $(EXTENSION_DIR)/bin/kindle-dashboard
	tar -C $(EXTENSION_ROOT) -czf $(EXTENSION_ARCHIVE) kindle-dashboard
	@echo "Packaged $(EXTENSION_ARCHIVE)"

extension-zig: $(ZIG_TARGET_BINARY)
	rm -rf $(EXTENSION_ROOT)
	mkdir -p $(EXTENSION_DIR)/bin
	mkdir -p $(EXTENSION_DIR)/assets
	cp ../kual/kindle-dashboard/config.xml $(EXTENSION_DIR)/config.xml
	cp ../kual/kindle-dashboard/config.sh.example $(EXTENSION_DIR)/config.sh.example
	cp ../kual/kindle-dashboard/menu.json $(EXTENSION_DIR)/menu.json
	if [ -d ../kual/kindle-dashboard/assets/recipes ]; then cp -R ../kual/kindle-dashboard/assets/recipes $(EXTENSION_DIR)/assets/recipes; fi
	cp ../kual/kindle-dashboard/assets/cookbook-icon.pgm $(EXTENSION_DIR)/assets/cookbook-icon.pgm
	cp ../kual/kindle-dashboard/bin/menu-ping.sh $(EXTENSION_DIR)/bin/menu-ping.sh
	cp ../kual/kindle-dashboard/bin/start.sh $(EXTENSION_DIR)/bin/start.sh
	cp ../kual/kindle-dashboard/bin/start-light.sh $(EXTENSION_DIR)/bin/start-light.sh
	cp ../kual/kindle-dashboard/bin/start-dark.sh $(EXTENSION_DIR)/bin/start-dark.sh
	cp ../kual/kindle-dashboard/bin/once.sh $(EXTENSION_DIR)/bin/once.sh
	cp ../kual/kindle-dashboard/bin/once-light.sh $(EXTENSION_DIR)/bin/once-light.sh
	cp ../kual/kindle-dashboard/bin/once-dark.sh $(EXTENSION_DIR)/bin/once-dark.sh
	cp ../kual/kindle-dashboard/bin/proof.sh $(EXTENSION_DIR)/bin/proof.sh
	cp ../kual/kindle-dashboard/bin/stop.sh $(EXTENSION_DIR)/bin/stop.sh
	cp ../kual/kindle-dashboard/bin/dashboard.sh $(EXTENSION_DIR)/bin/dashboard.sh
	cp ../kual/kindle-dashboard/bin/diagnose.sh $(EXTENSION_DIR)/bin/diagnose.sh
	cp $(ZIG_TARGET_BINARY) $(EXTENSION_DIR)/bin/kindle-dashboard
	chmod +x $(EXTENSION_DIR)/bin/menu-ping.sh $(EXTENSION_DIR)/bin/start.sh $(EXTENSION_DIR)/bin/start-light.sh $(EXTENSION_DIR)/bin/start-dark.sh $(EXTENSION_DIR)/bin/once.sh $(EXTENSION_DIR)/bin/once-light.sh $(EXTENSION_DIR)/bin/once-dark.sh $(EXTENSION_DIR)/bin/proof.sh $(EXTENSION_DIR)/bin/stop.sh $(EXTENSION_DIR)/bin/dashboard.sh $(EXTENSION_DIR)/bin/diagnose.sh $(EXTENSION_DIR)/bin/kindle-dashboard
	tar -C $(EXTENSION_ROOT) -czf $(EXTENSION_ARCHIVE) kindle-dashboard
	@echo "Packaged $(EXTENSION_ARCHIVE)"

check: $(TARGET)
	$(TARGET) --render fixtures/dashboard-data.json

clean:
	rm -rf $(BUILD_DIR)
