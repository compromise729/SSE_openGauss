# 源文件和目标文件
SRC = test.cpp
TARGET = test

# 默认目标
all: $(TARGET)

# 编译规则
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

# 清理
clean:
	rm -f $(TARGET)
