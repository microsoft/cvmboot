PROJECT_NAME := akvsign
CARGO := ${HOME}/.cargo/bin/cargo
# Build directory
TARGET_DIR := target
# Default build mode
BUILD_MODE := release

# Determine the binary name based on the build mode
ifeq ($(BUILD_MODE),release)
    BINARY := $(TARGET_DIR)/release/$(PROJECT_NAME)
    CARGO_FLAGS := --release
else
    BINARY := $(TARGET_DIR)/debug/$(PROJECT_NAME)
    CARGO_FLAGS :=
endif

.PHONY: all build clean help

# Default target
all: build

build:
	@echo "Building $(PROJECT_NAME) in $(BUILD_MODE) mode..."
	$(CARGO) build $(CARGO_FLAGS)

clean:
	@echo "Cleaning $(PROJECT_NAME)..."
	$(CARGO) clean
	rm -f $(BINARY)

run:
	@echo "Running $(PROJECT_NAME)..."
	$(BINARY) --help

test:
	@echo "Running $(PROJECT_NAME)..."
	$(CARGO) test $(CARGO_FLAGS)

distclean: clean
