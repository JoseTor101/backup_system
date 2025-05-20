# Compiler and flags
CXX = g++
CXXFLAGS = -lzip -std=c++17

# Target executable
TARGET = main

# Source files
SRCS = main.cpp compress.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Default target
all: $(TARGET) 

# Build the target
$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS)

# Rule to build object files
%.o: %.cpp
	$(CXX) -c $< $(CXXFLAGS)


# Clean up build files
clean:
	rm -f $(TARGET) $(OBJS)