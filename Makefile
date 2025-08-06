CC=clang
CLANG=clang

OUT_BIN=out/bin
OUT_KERNEL=out/kernel

CFLAGS=-Wall -Wextra -g -fno-omit-frame-pointer -fsanitize=address
LDFLAGS=-lOpenCL

C_SRCS := $(wildcard runner/*.c)
CL_SRCS := $(wildcard kernel/*.cl)

BIN_TARGETS := $(patsubst runner/%.c,$(OUT_BIN)/%,${C_SRCS})
KERNEL_TARGETS := $(patsubst kernel/%.cl,$(OUT_KERNEL)/%.spv,${CL_SRCS})

all: $(BIN_TARGETS) $(KERNEL_TARGETS)

$(OUT_BIN)/%: runner/%.c | $(OUT_BIN)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(OUT_KERNEL)/%.spv: kernel/%.cl | $(OUT_KERNEL)
	$(CLANG) -target spirv64 -c $< -o $@

.PHONY: runner/%.c kernel/%.cl

runner/%.c:
	@$(MAKE) $(OUT_BIN)/$(basename $(notdir $@))

kernel/%.cl:
	@$(MAKE) $(OUT_KERNEL)/$(basename $(notdir $@)).spv

.PHONY: build-kernel build-runner

build-kernel/%:
	@if [ ! -f kernel/$*.cl ]; then \
		echo "kernel/$*.cl: File not found"; \
		echo "Available Kernel Files:"; \
		ls kernel/*.cl 2>/dev/null || echo "  (Nothing)"; \
		exit 1; \
	fi
	$(MAKE) $(OUT_KERNEL)/$*.spv

build-runner/%:
	@if [ ! -f runner/$*.c ]; then \
		echo "runner/$*.c: File not found"; \
		echo "Available Runner Files:"; \
		ls runner/*.c 2>/dev/null || echo "  (Nothing)"; \
		exit 1; \
	fi
	$(MAKE) $(OUT_BIN)/$*

list:
	@echo "Available Files:"
	@echo "Kernel Files:"
	@ls kernel/*.cl 2>/dev/null | sed 's/kernel\///g; s/\.cl$$//g; s/^/  /' || echo "  (なし)"
	@echo "Runner Files:"
	@ls runner/*.c 2>/dev/null | sed 's/runner\///g; s/\.c$$//g; s/^/  /' || echo "  (なし)"

$(OUT_BIN):
	mkdir -p $@
$(OUT_KERNEL):
	mkdir -p $@

.PHONY: all clean
clean:
	rm -rf out/
