CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET := coding_agent

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
