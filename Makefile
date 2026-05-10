CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -fcolor-diagnostics -fansi-escape-codes -g
OPENMP_FLAGS = -Xclang -fopenmp
INCLUDE_DIRS = -Iinclude -I/usr/local/include -I/opt/homebrew/opt/libomp/include
LIB_DIRS = -L/usr/local/lib
LIBS = /opt/homebrew/opt/libomp/lib/libomp.a

# 源文件和目标
SOURCES_LIB = src/core.cpp src/gro.cpp
SOURCES_TEST = tests/gro_test.cpp
OBJECTS_LIB = $(SOURCES_LIB:.cpp=.o)
OBJECTS_TEST = $(SOURCES_TEST:.cpp=.o)

TARGET = gro_test

.PHONY: all build clean test help

# 默认目标 - 直接运行测试
all: test

# 构建项目
build: $(TARGET)
	@echo "✓ 构建完成"

# 链接可执行文件
$(TARGET): $(OBJECTS_LIB) $(OBJECTS_TEST)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -o $@ $^ $(LIB_DIRS) $(LIBS)

# 编译 .cpp 文件为 .o 文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) $(INCLUDE_DIRS) -c $< -o $@

# 运行测试
test: build
	./$(TARGET) config/config.yaml
	@echo "✓ 测试完成"

# 清理构建输出
clean:
	@rm -f $(OBJECTS_LIB) $(OBJECTS_TEST) $(TARGET)
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
