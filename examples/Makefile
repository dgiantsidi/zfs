CC = gcc # Flag for implicit rules
CXX = g++
CFLAGS = -g # Flag for implicit rules. Turn on debug info
CXXFLAGS = -g -Wall -O3 # Flag for implicit rules. Turn on debug info


examples = example_fsync example_fsync_large example_osync example_mmapped_file example_fsync_and_crash example_fsync_noenter
target: all

# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -Wall -Wextra -std=c++17

# Source files
SRCS = $(wildcard example*.cpp)

# Object files
OBJS = $(SRCS:.cpp=.o)

# Executable names (derived from source file names)
EXECS = $(SRCS:.cpp=)

# Default target
all: $(EXECS)

# Link object files to create executables
$(EXECS): %: %.o
	$(CXX) $< -o $@

# Compile source files to object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up generated files
clean:
	rm -f $(OBJS) $(EXECS)

.PHONY: all clean