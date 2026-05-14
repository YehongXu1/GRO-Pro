CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -fcolor-diagnostics -fansi-escape-codes -g
OPENMP_FLAGS = -Xclang -fopenmp
INCLUDE_DIRS = -Iinclude -I/usr/local/include -I/opt/homebrew/opt/libomp/include
LIB_DIRS = -L/usr/local/lib
LIBS = /opt/homebrew/opt/libomp/lib/libomp.a
TEST_CONFIG ?= config/test_config.yaml

# 源文件和目标
SOURCES_LIB = src/core.cpp src/gro.cpp src/svp.cpp src/gor.cpp src/sor.cpp src/fahl.cpp
OBJECTS_LIB = $(SOURCES_LIB:.cpp=.o)
GRO_TEST_OBJECTS = tests/gro_test.o
GRO_BASELINE_TEST_OBJECTS = tests/gro_baseline_test.o
SVP_TEST_OBJECTS = tests/svp_test.o
GOR_TEST_OBJECTS = tests/gor_test.o
SOR_TEST_OBJECTS = tests/sor_test.o
FAHL_TEST_OBJECTS = tests/fahl_test.o

TARGETS = gro_test gro_baseline_test svp_test gor_test sor_test fahl_test

.PHONY: all build clean test run-gro run-gro-baseline run-svp run-gor run-sor run-fahl help

# 默认目标 - 只构建，不运行测试或实验
all: build

# 构建项目
build: $(TARGETS)
	@echo "✓ 构建完成"

# 链接可执行文件
gro_test: $(OBJECTS_LIB) $(GRO_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $^ $(LIB_DIRS) $(LIBS)

gro_baseline_test: $(OBJECTS_LIB) $(GRO_BASELINE_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $^ $(LIB_DIRS) $(LIBS)

svp_test: $(OBJECTS_LIB) $(SVP_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $^ $(LIB_DIRS) $(LIBS)

gor_test: $(OBJECTS_LIB) $(GOR_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $^ $(LIB_DIRS) $(LIBS)

sor_test: $(OBJECTS_LIB) $(SOR_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $^ $(LIB_DIRS) $(LIBS)

fahl_test: $(OBJECTS_LIB) $(FAHL_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $^ $(LIB_DIRS) $(LIBS)

# 编译 .cpp 文件为 .o 文件
%.o: %.cpp
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
	@rm -f $(OBJECTS_LIB) $(GRO_TEST_OBJECTS) $(GRO_BASELINE_TEST_OBJECTS) $(SVP_TEST_OBJECTS) $(GOR_TEST_OBJECTS) $(SOR_TEST_OBJECTS) $(FAHL_TEST_OBJECTS) $(TARGETS)
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
	@echo "  make run-svp  - 只运行 svp_test"
	@echo "  make run-gor  - 只运行 gor_test"
	@echo "  make run-sor  - 只运行 sor_test"
	@echo "  make run-fahl - 只运行 fahl_test"
	@echo "  make run-gro TEST_CONFIG=config/config.yaml - 指定配置文件运行"
	@echo "  make clean    - 清理编译文件"
	@echo "  make rebuild  - 清理并重新构建"
	@echo "  make help     - 显示此帮助信息"
