CC ?= cc
BUILD ?= .host-build
CORE := components/vl_core
TEXT := components/vl_text
WARN := -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes
CORE_SOURCES := $(wildcard $(CORE)/*.c)
CORE_HEADERS := $(wildcard $(CORE)/include/*.h)
TEXT_SOURCES := $(wildcard $(TEXT)/*.c)
TEXT_HEADERS := $(wildcard $(TEXT)/include/*.h)

.PHONY: test
test: $(BUILD)/test_core $(BUILD)/test_text
	$(BUILD)/test_core
	$(BUILD)/test_text

$(BUILD)/test_core: $(CORE_SOURCES) $(CORE_HEADERS) tests/test_core.c
	mkdir -p $(BUILD)
	$(CC) -std=c11 $(WARN) $(CPPFLAGS) $(CFLAGS) -I$(CORE)/include $(CORE_SOURCES) tests/test_core.c $(LDFLAGS) -o $@

$(BUILD)/test_text: $(TEXT_SOURCES) $(TEXT_HEADERS) $(CORE_HEADERS) tests/test_text.c
	mkdir -p $(BUILD)
	$(CC) -std=c11 $(WARN) $(CPPFLAGS) $(CFLAGS) -I$(CORE)/include -I$(TEXT)/include $(TEXT_SOURCES) tests/test_text.c $(LDFLAGS) -o $@
