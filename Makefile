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

# 源文件和目标
SOURCES_LIB = src/core.cpp src/gro.cpp src/gro_baseline.cpp src/svp.cpp src/gor.cpp src/sor.cpp src/fahl.cpp
OBJECTS_LIB = $(SOURCES_LIB:.cpp=.o)
GRO_TEST_OBJECTS = tests/gro_test.o
GRO_BASELINE_TEST_OBJECTS = tests/gro_baseline_test.o
GRO_SELECTION_DEBUG_TEST_OBJECTS = tests/gro_selection_debug_test.o
GRO_REROUTE_DEBUG_TEST_OBJECTS = tests/gro_reroute_debug_test.o
MH_SYNTHETIC_EXPERIMENT_OBJECTS = tests/mh_synthetic_experiment.o
SVP_TEST_OBJECTS = tests/svp_test.o
GOR_TEST_OBJECTS = tests/gor_test.o
SOR_TEST_OBJECTS = tests/sor_test.o
FAHL_TEST_OBJECTS = tests/fahl_test.o

TARGETS = gro_test gro_baseline_test gro_selection_debug_test gro_reroute_debug_test mh_synthetic_experiment svp_test gor_test sor_test fahl_test

.PHONY: all build clean test run-gro run-gro-baseline run-gro-selection-debug run-gro-reroute-debug run-svp run-gor run-sor run-fahl help

# 默认目标 - 只构建，不运行测试或实验
all: build

# 构建项目
build: $(TARGETS)
	@echo "✓ 构建完成"

# 链接可执行文件
gro_test: $(OBJECTS_LIB) $(GRO_TEST_OBJECTS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gro_baseline_test: $(OBJECTS_LIB) $(GRO_BASELINE_TEST_OBJECTS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gro_selection_debug_test: $(OBJECTS_LIB) $(GRO_SELECTION_DEBUG_TEST_OBJECTS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gro_reroute_debug_test: $(OBJECTS_LIB) $(GRO_REROUTE_DEBUG_TEST_OBJECTS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

mh_synthetic_experiment: $(OBJECTS_LIB) $(MH_SYNTHETIC_EXPERIMENT_OBJECTS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

svp_test: $(OBJECTS_LIB) $(SVP_TEST_OBJECTS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

gor_test: $(OBJECTS_LIB) $(GOR_TEST_OBJECTS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

sor_test: $(OBJECTS_LIB) $(SOR_TEST_OBJECTS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

fahl_test: $(OBJECTS_LIB) $(FAHL_TEST_OBJECTS) Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $(filter-out Makefile,$^) $(LIB_DIRS) $(LIBS)

# 编译 .cpp 文件为 .o 文件
%.o: %.cpp Makefile
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) $(INCLUDE_DIRS) -c $< -o $@

# 运行测试
test: build
	./gro_test $(TEST_CONFIG)
	./gro_baseline_test $(TEST_CONFIG)
	./svp_test $(TEST_CONFIG)
	./gor_test $(TEST_CONFIG)
	./sor_test $(TEST_CONFIG)
	./fahl_test $(TEST_CONFIG)
	@echo "✓ 测试完成"

# 单独运行某个测试
run-gro: gro_test
	./gro_test $(TEST_CONFIG)

run-gro-baseline: gro_baseline_test
	./gro_baseline_test $(TEST_CONFIG)

run-gro-selection-debug: gro_selection_debug_test
	./gro_selection_debug_test $(TEST_CONFIG)

run-gro-reroute-debug: gro_reroute_debug_test
	./gro_reroute_debug_test $(TEST_CONFIG)

run-svp: svp_test
	./svp_test $(TEST_CONFIG)

run-gor: gor_test
	./gor_test $(TEST_CONFIG)

run-sor: sor_test
	./sor_test $(TEST_CONFIG)

run-fahl: fahl_test
	./fahl_test $(TEST_CONFIG)

# 清理构建输出
clean:
	@rm -f $(OBJECTS_LIB) $(GRO_TEST_OBJECTS) $(GRO_BASELINE_TEST_OBJECTS) $(GRO_SELECTION_DEBUG_TEST_OBJECTS) $(GRO_REROUTE_DEBUG_TEST_OBJECTS) $(MH_SYNTHETIC_EXPERIMENT_OBJECTS) $(SVP_TEST_OBJECTS) $(GOR_TEST_OBJECTS) $(SOR_TEST_OBJECTS) $(FAHL_TEST_OBJECTS) $(TARGETS)
	@echo "✓ 清理完成"

# 重新构建
rebuild: clean build

# 显示帮助信息
help:
	@echo "可用命令："
	@echo "  make          - 构建项目"
	@echo "  make test     - 使用小数据运行测试"
	@echo "  make run-gro  - 只运行 gro_test"
	@echo "  make run-gro-baseline - 只运行 gro_baseline_test"
	@echo "  make run-gro-selection-debug - 只运行 gro_selection_debug_test"
	@echo "  make run-gro-reroute-debug - 只运行 gro_reroute_debug_test"
	@echo "  make run-svp  - 只运行 svp_test"
	@echo "  make run-gor  - 只运行 gor_test"
	@echo "  make run-sor  - 只运行 sor_test"
	@echo "  make run-fahl - 只运行 fahl_test"
	@echo "  make run-gro TEST_CONFIG=config/config.yaml - 指定配置文件运行"
	@echo "  Linux/server: make"
	@echo "  macOS Homebrew GCC: make CXX=g++-14 （如果自动检测不到）"
	@echo "  make clean    - 清理编译文件"
	@echo "  make rebuild  - 清理并重新构建"
	@echo "  make help     - 显示此帮助信息"
