CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -fcolor-diagnostics -fansi-escape-codes -g
OPENMP_FLAGS = -Xclang -fopenmp
INCLUDE_DIRS = -Iinclude -I/usr/local/include -I/opt/homebrew/opt/libomp/include
LIB_DIRS = -L/usr/local/lib
LIBS = /opt/homebrew/opt/libomp/lib/libomp.a

# 源文件和目标
SOURCES_LIB = src/core.cpp src/gro.cpp src/svp.cpp src/gor.cpp src/sor.cpp src/fahl.cpp
OBJECTS_LIB = $(SOURCES_LIB:.cpp=.o)
GRO_TEST_OBJECTS = tests/gro_test.o
SVP_TEST_OBJECTS = tests/svp_test.o
GOR_TEST_OBJECTS = tests/gor_test.o
SOR_TEST_OBJECTS = tests/sor_test.o
FAHL_TEST_OBJECTS = tests/fahl_test.o

TARGETS = gro_test svp_test gor_test sor_test fahl_test

.PHONY: all build clean test help

# 默认目标 - 直接运行测试
all: test

# 构建项目
build: $(TARGETS)
	@echo "✓ 构建完成"

# 链接可执行文件
gro_test: $(OBJECTS_LIB) $(GRO_TEST_OBJECTS)
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
	./gro_test config/config.yaml
	./svp_test config/config.yaml
	./gor_test config/config.yaml
	./sor_test config/config.yaml
	./fahl_test config/config.yaml
	@echo "✓ 测试完成"

# 清理构建输出
clean:
	@rm -f $(OBJECTS_LIB) $(GRO_TEST_OBJECTS) $(SVP_TEST_OBJECTS) $(GOR_TEST_OBJECTS) $(SOR_TEST_OBJECTS) $(FAHL_TEST_OBJECTS) $(TARGETS)
	@echo "✓ 清理完成"

# 重新构建
rebuild: clean build

# 显示帮助信息
help:
	@echo "可用命令："
	@echo "  make          - 构建项目"
	@echo "  make test     - 运行测试"
	@echo "  make clean    - 清理编译文件"
	@echo "  make rebuild  - 清理并重新构建"
	@echo "  make help     - 显示此帮助信息"
