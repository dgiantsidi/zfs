CXX_FLAGS := -O3 -Wall -Wextra -pthread -I../include 
#CXX_FLAGS := -O3 -Wno-declaration-after-statement -fsanitize=address -g -Wall -Wextra -pthread

all: clean
	$(CXX) $(CXX_FLAGS) user_space_proc.cc -o user_space_proc_cpp #-fsanitize=address
	
clean:
	rm -f user_space_proc_cpp
	