CC=clang
CLANG=clang

OUT_BIN=out/bin
OUT_KERNEL=out/kernel
OUT_RUNTIME=out/runtime
OUT_OBJ=out/obj

INCLUDE_DIRS=common/include
CFLAGS=-Wall -Wextra -g -fno-omit-frame-pointer -fsanitize=address $(addprefix -I,$(INCLUDE_DIRS))
LDFLAGS=-lOpenCL

C_SRCS := $(wildcard runner/*.c)
CL_SRCS := $(wildcard kernel/*.cl)
COMMON_SRCS := $(wildcard common/*.c)
RUNTIME_SRC := runtime/lib.cl

BIN_TARGETS := $(patsubst runner/%.c,$(OUT_BIN)/%,${C_SRCS})
KERNEL_TARGETS := $(patsubst kernel/%.cl,$(OUT_KERNEL)/%.spv,${CL_SRCS})
COMMON_OBJS := $(patsubst common/%.c,$(OUT_OBJ)/%.o,${COMMON_SRCS})
RUNTIME_TARGETS := $(patsubst runtime/%.cl,$(OUT_RUNTIME)/%.spv,${RUNTIME_SRCS})

all: $(BIN_TARGETS) $(OUT_RUNTIME)/libscsan_rt.spv $(KERNEL_TARGETS)

$(OUT_BIN) $(OUT_KERNEL) $(OUT_RUNTIME) $(OUT_OBJ):
	mkdir -p $@

$(OUT_BIN)/%: runner/%.c $(COMMON_OBJS) | $(OUT_BIN)
	$(CC) $(CFLAGS) $< $(COMMON_OBJS) -o $@ $(LDFLAGS)

$(OUT_OBJ)/%.o: common/%.c | $(OUT_OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_RUNTIME)/%.spv: runtime/%.cl | $(OUT_RUNTIME)
	$(CLANG) -target spirv64 -O2 -cl-std=CL3.0 -c $< -o $@

# Link runtime SPIRV files into libscsan_rt.spv
$(OUT_RUNTIME)/libscsan_rt.spv: $(RUNTIME_SRC) | $(OUT_RUNTIME)
	$(CLANG) -target spirv64 -O2 -cl-std=CL3.0 -c $^ -o $@

$(OUT_KERNEL)/%.spv: kernel/%.cl | $(OUT_KERNEL)
	$(CLANG) -target spirv64 -O2 -cl-std=CL3.0 -fpass-plugin=plugin/build/libSPIRVComputeSanitizer.so -Wl,--allow-pointer-mismatch -Wl,--use-highest-version $(OUT_RUNTIME)/libscsan_rt.spv $< -o $@

.PHONY: runner/%.c runtime/%.cl kernel/%.cl

runner/%.c:
	@$(MAKE) $(OUT_BIN)/$(basename $(notdir $@))

runtime/%.cl:
	@$(MAKE) $(OUT_RUNTIME)/$(basename $(notdir $@)).spv

kernel/%.cl:
	@$(MAKE) $(OUT_KERNEL)/$(basename $(notdir $@)).spv

.PHONY: build-runner build-kernel

build-runner/%:
	@if [ ! -f runner/$*.c ]; then \
		echo "runner/$*.c: File not found"; \
		echo "Available Runner Files:"; \
		ls runner/*.c 2>/dev/null || echo "  (Nothing)"; \
		exit 1; \
	fi
	$(MAKE) $(OUT_BIN)/$*

build-kernel/%:
	@if [ ! -f kernel/$*.cl ]; then \
		echo "kernel/$*.cl: File not found"; \
		echo "Available Kernel Files:"; \
		ls kernel/*.cl 2>/dev/null || echo "  (Nothing)"; \
		exit 1; \
	fi
	$(MAKE) $(OUT_KERNEL)/$*.spv

build-runtime/%:
	@if [ ! -f runtime/$*.cl ]; then \
		echo "runtime/$*.cl: File not found"; \
		echo "Available Runtime Files:"; \
		ls runtime/*.cl 2>/dev/null || echo "  (Nothing)"; \
		exit 1; \
	fi
	$(MAKE) $(OUT_RUNTIME)/$*.spv

list:
	@echo "Available Files:"
	@echo "Kernel Files:"
	@ls kernel/*.cl 2>/dev/null | sed 's/kernel\///g; s/\.cl$$//g; s/^/  /' || echo "  (なし)"
	@echo "Runner Files:"
	@ls runner/*.c 2>/dev/null | sed 's/runner\///g; s/\.c$$//g; s/^/  /' || echo "  (なし)"

.PHONY: all clean

clean:
	rm -rf out/
