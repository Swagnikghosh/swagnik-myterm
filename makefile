# Makefile for main.cpp using X11 and pthread

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O0
LIBS     = -lX11 -pthread

TARGET   = main
SRC      = main.cpp
OBJ      = $(SRC:.cpp=.o)

# Default target
all: $(TARGET)

# Link the program
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

# Compile source files into object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<

# Clean up build files
clean:
	rm -f $(OBJ) $(TARGET)

# Rebuild from scratch
rebuild: clean all

.PHONY: all clean rebuild
