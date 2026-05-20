UNAME_S := $(shell uname -s)
GNU_GXX_CANDIDATES := g++-15 g++-14 g++-13 g++-12 g++-11 g++-10
FOUND_GXX := $(firstword $(foreach c,$(GNU_GXX_CANDIDATES),$(shell command -v $(c) 2>/dev/null)))

ifeq ($(origin CXX),default)
  ifeq ($(UNAME_S),Darwin)
    ifneq ($(FOUND_GXX),)
      CXX = $(FOUND_GXX)
      DEFAULT_OPENMP_FLAGS = -fopenmp
      DEFAULT_INCLUDE_DIRS = -Iinclude
      DEFAULT_LIB_DIRS =
      DEFAULT_LIBS =
    else
      CXX = clang++
      DEFAULT_OPENMP_FLAGS = -Xclang -fopenmp
      DEFAULT_INCLUDE_DIRS = -Iinclude -I/usr/local/include -I/opt/homebrew/opt/libomp/include
      DEFAULT_LIB_DIRS = -L/usr/local/lib
      DEFAULT_LIBS = /opt/homebrew/opt/libomp/lib/libomp.a
    endif
  else
    CXX = g++
    DEFAULT_OPENMP_FLAGS = -fopenmp
    DEFAULT_INCLUDE_DIRS = -Iinclude
    DEFAULT_LIB_DIRS =
    DEFAULT_LIBS =
  endif
else
  DEFAULT_OPENMP_FLAGS = -fopenmp
  DEFAULT_INCLUDE_DIRS = -Iinclude
  DEFAULT_LIB_DIRS =
  DEFAULT_LIBS =
endif

CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -g
OPENMP_FLAGS ?= $(DEFAULT_OPENMP_FLAGS)
INCLUDE_DIRS ?= $(DEFAULT_INCLUDE_DIRS)
LIB_DIRS ?= $(DEFAULT_LIB_DIRS)
LIBS ?= $(DEFAULT_LIBS)

TEST_CONFIG ?= config/test_config.yaml
ABLATION_CONFIG ?= config/config.yaml
QUERY_DIR ?= data/MH_Synthetic_query_sets
RESULTS_DIR ?= python/results/mh
BJ_CONFIG ?= config/config_bj.yaml
BJ_QUERY_DIR ?= data/BJ_Synthetic_query_sets
BJ_RESULTS_DIR ?= python/results/bj
RANDOM_SEED ?= 0
FIXED_FRACTIONS ?= 10,30
TDG_GAMMAS ?= 50
IMPACT_WEIGHTS ?= 30

SOURCES_LIB = \
	src/core.cpp \
	src/gro.cpp \
	src/gro_baseline.cpp \
	src/svp.cpp \
	src/gor.cpp \
	src/sor.cpp \
	src/fahl.cpp

OBJECTS_LIB = $(SOURCES_LIB:.cpp=.o)
HEADERS = $(wildcard include/*.hpp)

TARGETS = \
	gro_test \
	gro_baseline_test \
	gro_selection_debug_test \
	gro_reroute_debug_test \
	gro_fixed_random_selection_test \
	gro_ablation_test \
	mh_synthetic_experiment \
	svp_test \
	gor_test \
	sor_test \
	fahl_test

TEST_OBJECTS = \
	tests/gro_test.o \
	tests/gro_baseline_test.o \
	tests/gro_selection_debug_test.o \
	tests/gro_reroute_debug_test.o \
	tests/gro_fixed_random_selection_test.o \
	tests/gro_ablation_test.o \
	tests/mh_synthetic_experiment.o \
	tests/svp_test.o \
	tests/gor_test.o \
	tests/sor_test.o \
	tests/fahl_test.o

ABLATION_METHODS = \
	baseline_random_normal \
	baseline_delayed_normal \
	baseline_random_tdg_reroute \
	baseline_delayed_tdg_reroute \
	tdg_anchor_normal \
	tdg_excess_normal \
	tdg_anchor_full \
	tdg_excess_full

.PHONY: \
	all build clean rebuild test help \
	run-gro run-gro-baseline run-gro-selection-debug run-gro-reroute-debug \
	run-gro-fixed-random-selection run-gro-ablation \
	run-svp run-gor run-sor run-fahl \
	run-ablation-methods merge-ablation-methods check-ablation-methods \
	run-ablation-baseline-random-normal \
	run-ablation-baseline-delayed-normal \
	run-ablation-baseline-random-tdg-reroute \
	run-ablation-baseline-delayed-tdg-reroute \
	run-ablation-tdg-anchor-normal \
	run-ablation-tdg-excess-normal \
	run-ablation-tdg-anchor-full \
	run-ablation-tdg-excess-full \
	run-bj-ablation-methods check-bj-ablation-methods merge-bj-ablation-methods

all: build

build: $(TARGETS)
	@echo "✓ 构建完成"

gro_test: $(OBJECTS_LIB) tests/gro_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gro_baseline_test: $(OBJECTS_LIB) tests/gro_baseline_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gro_selection_debug_test: $(OBJECTS_LIB) tests/gro_selection_debug_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gro_reroute_debug_test: $(OBJECTS_LIB) tests/gro_reroute_debug_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gro_fixed_random_selection_test: $(OBJECTS_LIB) tests/gro_fixed_random_selection_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gro_ablation_test: $(OBJECTS_LIB) tests/gro_ablation_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

mh_synthetic_experiment: $(OBJECTS_LIB) tests/mh_synthetic_experiment.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

svp_test: $(OBJECTS_LIB) tests/svp_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gor_test: $(OBJECTS_LIB) tests/gor_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

sor_test: $(OBJECTS_LIB) tests/sor_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

fahl_test: $(OBJECTS_LIB) tests/fahl_test.o Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

%.o: %.cpp $(HEADERS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) $(INCLUDE_DIRS) -c $< -o $@

test: build
	./gro_test $(TEST_CONFIG)
	./gro_baseline_test $(TEST_CONFIG)
	./svp_test $(TEST_CONFIG)
	./gor_test $(TEST_CONFIG)
	./sor_test $(TEST_CONFIG)
	./fahl_test $(TEST_CONFIG)
	@echo "✓ 测试完成"

run-gro: gro_test
	./gro_test $(TEST_CONFIG)

run-gro-baseline: gro_baseline_test
	./gro_baseline_test $(TEST_CONFIG)

run-gro-selection-debug: gro_selection_debug_test
	./gro_selection_debug_test $(TEST_CONFIG)

run-gro-reroute-debug: gro_reroute_debug_test
	./gro_reroute_debug_test $(TEST_CONFIG)

run-gro-fixed-random-selection: gro_fixed_random_selection_test
	./gro_fixed_random_selection_test $(TEST_CONFIG)

run-gro-ablation: gro_ablation_test
	./gro_ablation_test $(TEST_CONFIG)

run-svp: svp_test
	./svp_test $(TEST_CONFIG)

run-gor: gor_test
	./gor_test $(TEST_CONFIG)

run-sor: sor_test
	./sor_test $(TEST_CONFIG)

run-fahl: fahl_test
	./fahl_test $(TEST_CONFIG)

$(RESULTS_DIR):
	mkdir -p $(RESULTS_DIR)

define RUN_ABLATION
./gro_ablation_test $(ABLATION_CONFIG) \
  --query-dir $(QUERY_DIR) \
  --output $(RESULTS_DIR)/gro_ablation_$(1).csv \
  --selection-methods $(2) \
  --reroute-methods $(3) \
  --fixed-fractions $(FIXED_FRACTIONS) \
  --tdg-gammas $(TDG_GAMMAS) \
  --impact-weights $(IMPACT_WEIGHTS) \
  --random-seed $(RANDOM_SEED)
endef

run-ablation-methods: \
	run-ablation-baseline-random-normal \
	run-ablation-baseline-delayed-normal \
	run-ablation-baseline-random-tdg-reroute \
	run-ablation-baseline-delayed-tdg-reroute \
	run-ablation-tdg-anchor-normal \
	run-ablation-tdg-excess-normal \
	run-ablation-tdg-anchor-full \
	run-ablation-tdg-excess-full

run-ablation-baseline-random-normal: gro_ablation_test | $(RESULTS_DIR)
	$(call RUN_ABLATION,baseline_random_normal,random,normal)

run-ablation-baseline-delayed-normal: gro_ablation_test | $(RESULTS_DIR)
	$(call RUN_ABLATION,baseline_delayed_normal,most_delayed,normal)

run-ablation-baseline-random-tdg-reroute: gro_ablation_test | $(RESULTS_DIR)
	$(call RUN_ABLATION,baseline_random_tdg_reroute,random,tdg)

run-ablation-baseline-delayed-tdg-reroute: gro_ablation_test | $(RESULTS_DIR)
	$(call RUN_ABLATION,baseline_delayed_tdg_reroute,most_delayed,tdg)

run-ablation-tdg-anchor-normal: gro_ablation_test | $(RESULTS_DIR)
	$(call RUN_ABLATION,tdg_anchor_normal,tdg_anchor,normal)

run-ablation-tdg-excess-normal: gro_ablation_test | $(RESULTS_DIR)
	$(call RUN_ABLATION,tdg_excess_normal,tdg_excess,normal)

run-ablation-tdg-anchor-full: gro_ablation_test | $(RESULTS_DIR)
	$(call RUN_ABLATION,tdg_anchor_full,tdg_anchor,tdg)

run-ablation-tdg-excess-full: gro_ablation_test | $(RESULTS_DIR)
	$(call RUN_ABLATION,tdg_excess_full,tdg_excess,tdg)

run-bj-ablation-methods:
	$(MAKE) run-ablation-methods \
	  ABLATION_CONFIG=$(BJ_CONFIG) \
	  QUERY_DIR=$(BJ_QUERY_DIR) \
	  RESULTS_DIR=$(BJ_RESULTS_DIR) \
	  FIXED_FRACTIONS=$(FIXED_FRACTIONS) \
	  TDG_GAMMAS=$(TDG_GAMMAS) \
	  IMPACT_WEIGHTS=$(IMPACT_WEIGHTS) \
	  RANDOM_SEED=$(RANDOM_SEED)

check-bj-ablation-methods:
	$(MAKE) check-ablation-methods RESULTS_DIR=$(BJ_RESULTS_DIR)

merge-bj-ablation-methods:
	$(MAKE) merge-ablation-methods RESULTS_DIR=$(BJ_RESULTS_DIR)

merge-ablation-methods: | $(RESULTS_DIR)
	@first=1; \
	for method in $(ABLATION_METHODS); do \
	  file="$(RESULTS_DIR)/gro_ablation_$$method.csv"; \
	  if [ ! -f "$$file" ]; then \
	    echo "missing $$file"; \
	    exit 1; \
	  fi; \
	  if [ $$first -eq 1 ]; then \
	    head -1 "$$file" > "$(RESULTS_DIR)/gro_ablation.csv"; \
	    first=0; \
	  fi; \
	  tail -n +2 "$$file" >> "$(RESULTS_DIR)/gro_ablation.csv"; \
	done; \
	echo "wrote $(RESULTS_DIR)/gro_ablation.csv"

check-ablation-methods:
	@for method in $(ABLATION_METHODS); do \
	  file="$(RESULTS_DIR)/gro_ablation_$$method.csv"; \
	  if [ -f "$$file" ]; then \
	    wc -l "$$file"; \
	  else \
	    echo "missing $$file"; \
	  fi; \
	done

clean:
	@rm -f $(OBJECTS_LIB) $(TEST_OBJECTS) $(TARGETS)
	@echo "✓ 清理完成"

rebuild: clean build

help:
	@echo "Build:"
	@echo "  make                         - build all executables"
	@echo "  make gro_ablation_test        - build one executable"
	@echo "  make clean                    - remove build outputs"
	@echo "  make rebuild                  - clean and build"
	@echo ""
	@echo "Tests:"
	@echo "  make test                     - run core unit/smoke tests"
	@echo "  make run-gro TEST_CONFIG=...  - run gro_test with a config"
	@echo ""
	@echo "Experiments:"
	@echo "  make run-ablation-methods     - run all method-split ablations sequentially"
	@echo "  make run-bj-ablation-methods  - run all method-split ablations on BJ synthetic sets"
	@echo "  make merge-ablation-methods   - merge method CSVs into gro_ablation.csv"
	@echo "  make check-ablation-methods   - show method CSV row counts"
	@echo "  make check-bj-ablation-methods - show BJ method CSV row counts"
	@echo ""
	@echo "Overrides:"
	@echo "  ABLATION_CONFIG=config/config.yaml QUERY_DIR=data/MH_Synthetic_query_sets"
	@echo "  BJ_CONFIG=config/config_bj.yaml BJ_QUERY_DIR=data/BJ_Synthetic_query_sets"
	@echo "  RESULTS_DIR=python/results/mh BJ_RESULTS_DIR=python/results/bj"
	@echo "  FIXED_FRACTIONS=10,30 TDG_GAMMAS=50 IMPACT_WEIGHTS=30 RANDOM_SEED=0"
	@echo "  Linux/server: make"
	@echo "  macOS Homebrew GCC: make CXX=g++-14"
