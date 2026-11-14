# ca - build.
#
#   make                 build build/<cxx>-<mode>/libca.a (clang++, debug)
#   make test            build, then run tests/*.cpp, each its own target (-j runs them in
#                        parallel; see tooling/test.sh), then count them (tooling/test_summary.sh)
#   make test T=NAME     run only tests/NAME.cpp; globs and several names work:
#                        T='optional_self_assign dynamic_array_*'
#   make includes        regenerate the src/lib/ca/ include facade (`#include "ca/print.hpp"`;
#                        see tooling/gen_includes.py); every build runs it
#   make leaks           no system/std header reachable from a header, no symbol
#                        outside ca in the library (see tooling/leaks.sh)
#   make matrix          facade check, leaks + test with clang++ and g++, debug and release (T too);
#                        the four are independent (matrix-g++-release, ...), so -j runs them and
#                        their tests at once, lines tagged with the configuration
#   make bench           benchmarks against std (see bench/run.sh); B=path (or ring, ...) picks
#                        bench/path.cpp instead of bench/dynamic_array.cpp
#   make bench-doc       run every benchmark a header quotes (--BENCH blocks) with clang++ and
#                        g++, rewrite those tables in the headers, regenerate doc/ (autodoc
#                        --bench); B='hashmap map' runs only those
#   make syscalls        regenerate the ca::syscall table of this machine
#                        (see tooling/gen_syscalls.py)
#   make unicode         regenerate the ca::unicode character database and the classes of
#                        ca::regex from the running Python and the UCD of its Unicode version
#                        (see tooling/gen_unicode.py)
#   make gen             run every generator, in dependency order: traits, syscall table,
#                        unicode tables, include facade (derived from the tree), str_python
#                        and regex_python tests, doc (md and html)
#   make clean           remove objects (.o) and dependency files (.d) of every build, and the
#                        generators' scratch (downloads, bytecode: tooling/<gen>.py --clean)
#   make fclean          clean, then remove build/ entirely and everything the generators
#                        wrote (--fclean: traits, facade, syscall table, unicode tables,
#                        str_python and regex_python tests, doc); make gen brings them back
#
#   make sanitize        test under ASan and under UBSan, with clang++ and g++, debug (T too);
#                        the four are independent, -j runs them at once
#   make coverage        test with clang source coverage (T too), then build/coverage/: index.html,
#                        one page per source file, and summary.txt, lines and branches per file
#   make fuzz F=NAME     run fuzz/NAME.cpp with libFuzzer (clang++, ASan and UBSan) on its corpus
#                        build/fuzz/corpus/NAME, seeded from fuzz/seeds/NAME; TIME=seconds (60),
#                        JOBS=processes (1); crashes land in build/fuzz/crashes/NAME
#   make fuzz-smoke      every fuzz target on its seeds, then TIME (10) seconds each
#   make fuzz-regex      differential: random patterns and subjects through ca::regex and
#                        CPython's re (tooling/fuzz_regex.py); N=cases (20000), SEED=n
#
#   make CXX=g++ MODE=release
#   make SAN=address     build and test into build/<cxx>-<mode>-address: libc links only (freestanding
#                        tests skipped), ca::heap and ca::arena poison what they do not hand out
#                        (ca_memory_poisoning); SAN=undefined: UBSan without its runtime, each check
#                        a ca trap (3-system/sanitizer/ubsan.cpp), freestanding links too;
#                        SAN=address,undefined both
#   make MODE=release FRAME_POINTERS=1   keep frame pointers, for full trap backtraces
#                        (ca::trace::backtrace); on by default in debug; make clean after
#                        changing it, objects are not rebuilt for it
#
# Sources are every .cpp under src/, found recursively. Each compiles to its
# own .o under the build directory, mirroring the source tree. -MMD -MP emit a
# .d per object listing the headers it includes, so touching a header rebuilds
# exactly the objects that use it, and deleting one does not break the build.
#
# Debug builds use -O1: the compile-time check of unchecked accesses relies on
# the optimizer proving them.

ifeq ($(origin CXX),default)
CXX := clang++
endif
MODE ?= debug

ifneq ($(findstring clang,$(CXX)),)
STD ?= c++2c
else
STD ?= c++23
endif

CPPFLAGS := -Isrc/lib -MMD -MP
CXXFLAGS := -std=$(STD) -nostdinc++ -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror -pthread
LDFLAGS  := -nostdinc++ -pthread
ifneq ($(findstring clang,$(CXX)),)
LDFLAGS  += -nostdlib++
endif

ifeq ($(MODE),release)
CXXFLAGS += -O2 -DNDEBUG
FRAME_POINTERS ?= 0
else ifeq ($(MODE),debug)
CXXFLAGS += -g -O1
FRAME_POINTERS ?= 1
else
$(error MODE must be debug or release)
endif

ifeq ($(FRAME_POINTERS),1)
CXXFLAGS += -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer
endif

comma := ,
SAN ?=
COVERAGE ?= 0
SUFFIX :=
ifneq ($(SAN),)
CXXFLAGS += -fsanitize=$(SAN) -fno-sanitize-recover=all -fno-omit-frame-pointer
SUFFIX := -$(subst $(comma),-,$(SAN))
ifneq ($(filter address,$(subst $(comma), ,$(SAN))),)
LDFLAGS += -fsanitize=address
ifneq ($(findstring clang,$(CXX)),)
LDFLAGS += --driver-mode=gcc
endif
endif
endif
ifeq ($(COVERAGE),1)
CXXFLAGS += -fprofile-instr-generate -fcoverage-mapping
LDFLAGS  += -fprofile-instr-generate
SUFFIX := $(SUFFIX)-coverage
endif

GENERATORS := gen_traits gen_syscalls gen_unicode gen_float_tables gen_includes gen_str_test gen_regex_test autodoc

BUILD := build/$(notdir $(CXX))-$(MODE)$(SUFFIX)
LIB   := $(BUILD)/libca.a
SRCS  := $(shell find src/lib -type f -name '*.cpp')
OBJS  := $(SRCS:%.cpp=$(BUILD)/%.o)

RUNTIME      := $(BUILD)/libca_runtime.a
RUNTIME_SRCS := $(shell find src/runtime -type f -name '*.cpp')
RUNTIME_OBJS := $(RUNTIME_SRCS:%.cpp=$(BUILD)/%.o)

DEPS  := $(OBJS:.o=.d) $(RUNTIME_OBJS:.o=.d)

TEST_MATCHES := $(foreach p,$(or $(T),*),$(or $(wildcard tests/$(p).cpp),$(error no test matches '$(p)')))
TEST_NAMES   := $(sort $(basename $(notdir $(TEST_MATCHES))))
TEST_RESULTS := $(TEST_NAMES:%=$(BUILD)/tests/%.result)

CONFIGS := clang++-debug clang++-release g++-debug g++-release
SANITIZE_CONFIGS := clang++-address clang++-undefined g++-address g++-undefined

FUZZ_SAN   := fuzzer-no-link,address,undefined
FUZZ_NAMES := $(sort $(basename $(notdir $(wildcard fuzz/*.cpp))))
FUZZ_BINS  := $(FUZZ_NAMES:%=$(BUILD)/fuzz/%)

.PHONY: all includes test leaks matrix matrix-includes $(CONFIGS:%=matrix-%) sanitize $(SANITIZE_CONFIGS:%=sanitize-%) \
	coverage fuzz fuzz-smoke fuzz-regex fuzz-build bench bench-doc syscalls unicode gen clean fclean FORCE

all: includes $(LIB) $(RUNTIME)

includes:
ifeq ($(MATRIX),)
	@python3 tooling/gen_includes.py
endif

$(LIB): $(OBJS)
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

$(RUNTIME): $(RUNTIME_OBJS)
	@mkdir -p $(@D)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

test: $(TEST_RESULTS)
	@CXX='$(CXX)' MODE='$(MODE)' BUILD='$(BUILD)' tooling/test_summary.sh $(TEST_NAMES)

$(BUILD)/tests/%.result: tests/%.cpp $(LIB) $(RUNTIME) FORCE | includes
	@CXX='$(CXX)' MODE='$(MODE)' CXXFLAGS='$(CPPFLAGS) $(CXXFLAGS)' LDFLAGS='$(LDFLAGS)' \
		LIB='$(LIB)' RUNTIME='$(RUNTIME)' BUILD='$(BUILD)' TAG='$(TAG)' SAN='$(SAN)' \
		COVERAGE='$(COVERAGE)' tooling/test.sh $<

FORCE:

leaks: $(LIB)
	@CXX='$(CXX)' CXXFLAGS='$(CPPFLAGS) $(CXXFLAGS)' LIB='$(LIB)' tooling/leaks.sh

matrix: $(CONFIGS:%=matrix-%)

matrix-includes:
	@python3 tooling/gen_includes.py --check

$(CONFIGS:%=matrix-%): matrix-%: matrix-includes
	@$(MAKE) --no-print-directory MATRIX=1 CXX=$(word 1,$(subst -, ,$*)) MODE=$(word 2,$(subst -, ,$*)) \
		TAG='$(subst -, ,$*)' T='$(T)' leaks test

sanitize: $(SANITIZE_CONFIGS:%=sanitize-%)

$(SANITIZE_CONFIGS:%=sanitize-%): sanitize-%: matrix-includes
	@$(MAKE) --no-print-directory MATRIX=1 CXX=$(word 1,$(subst -, ,$*)) MODE=debug SAN=$(word 2,$(subst -, ,$*)) \
		TAG='$(subst -, ,$*)' T='$(T)' test

coverage: includes
	@$(MAKE) --no-print-directory MATRIX=1 CXX=clang++ MODE=debug COVERAGE=1 T='$(T)' test || true
	@BUILD=build/clang++-debug-coverage tooling/coverage.sh build/coverage

fuzz: includes
	@test -n '$(F)' || { echo 'make fuzz F=NAME, NAME one of: $(FUZZ_NAMES)'; exit 1; }
	@$(MAKE) --no-print-directory MATRIX=1 CXX=clang++ MODE=debug SAN=$(FUZZ_SAN) build/clang++-debug-$(subst $(comma),-,$(FUZZ_SAN))/fuzz/$(F) \
		build/fuzz/seeds/elf/object.o build/fuzz/seeds/elf/shared.so
	@tooling/fuzz.sh build/clang++-debug-$(subst $(comma),-,$(FUZZ_SAN))/fuzz/$(F) $(or $(TIME),60) $(or $(JOBS),1)

fuzz-smoke: includes
	@$(MAKE) --no-print-directory MATRIX=1 CXX=clang++ MODE=debug SAN=$(FUZZ_SAN) fuzz-build
	@status=0; for f in $(FUZZ_NAMES); do \
		tooling/fuzz.sh build/clang++-debug-$(subst $(comma),-,$(FUZZ_SAN))/fuzz/$$f $(or $(TIME),10) 1 || status=1; \
	done; exit $$status

fuzz-build: $(FUZZ_BINS) build/fuzz/seeds/elf/object.o build/fuzz/seeds/elf/shared.so

build/fuzz/seeds/elf/object.o: tests/dl/base.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -std=$(STD) -nostdinc++ -fno-exceptions -fno-rtti -g -O1 -fPIC -c $< -o $@

build/fuzz/seeds/elf/shared.so: tests/dl/base.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -std=$(STD) -nostdinc++ -fno-exceptions -fno-rtti -g -O1 -fPIC -shared -nostdlib -fno-stack-protector $< -o $@

$(BUILD)/fuzz/%: fuzz/%.cpp fuzz/fuzz.hpp $(LIB)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) -I. $(subst fuzzer-no-link,fuzzer,$(CXXFLAGS)) $< -o $@ $(LIB) -pthread

fuzz-regex: all
	@CXX='$(CXX)' CXXFLAGS='$(CPPFLAGS) $(CXXFLAGS)' LDFLAGS='$(LDFLAGS)' LIB='$(LIB)' BUILD='$(BUILD)' \
		python3 tooling/fuzz_regex.py --cases $(or $(N),20000) $(if $(SEED),--seed $(SEED))

bench:
	@bench/run.sh -c $(CXX) -b $(or $(B),dynamic_array)

bench-doc:
	@python3 tooling/autodoc.py --bench $(B)
	@python3 tooling/autodoc.py -f html

syscalls:
	python3 tooling/gen_syscalls.py

unicode:
	python3 tooling/gen_unicode.py

gen:
	@for g in $(GENERATORS); do python3 tooling/$$g.py || exit 1; done
	@python3 tooling/autodoc.py -f html

clean:
	@if [ -d build ]; then find build \( -name '*.o' -o -name '*.d' \) -type f -delete; find build -mindepth 1 -type d -empty -delete; fi
	@for g in $(GENERATORS); do python3 tooling/$$g.py --clean || exit 1; done

fclean: clean
	rm -rf build
	@for g in $(GENERATORS); do python3 tooling/$$g.py --fclean || exit 1; done

-include $(DEPS)
