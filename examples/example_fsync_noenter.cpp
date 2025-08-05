#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory>

void wait_for_enter(const std::string &msg) {
//	std::cout << "Press Enter to continue.. " << msg << " has been executed.\n";
//	std::cin.get();
}

int main(int args, char* argv[]) {
	int fd = -1;
	std::string fname = argv[1];
	std::string filepath = "/"+fname+"/example.txt";
	fd = open(filepath.c_str(),
			O_RDWR|O_CREAT); // Specify the correct path to your ZFS file system

	if (fd < -1) {
		printf("Error opening file!\n");
		exit(1);
	}

	wait_for_enter("open()");

	std::string to_be_written = "hello Dimitra" +std::string(argv[2]);
	//to_be_written += argv[2];
	auto len = write(fd, to_be_written.c_str(), to_be_written.size());
	while (len < to_be_written.size()) {
		auto offset = len;
		auto remaining = to_be_written.size() - len;
		len += write(fd, to_be_written.c_str() + offset, remaining);
		std::cout << "written_bytes=" << len << " remaining=" << (to_be_written.size() - len) << "\n";

	}
	if (fsync(fd) == -1) {
		perror("Error synchronizing file");
		close(fd);
		return 1;
	}
#if 0
	for (auto i = 0ULL; i < 10; i++)
		to_be_written+=to_be_written;

	for (auto i = 0ULL; i < 1; i++) {
		auto len = write(fd, to_be_written.c_str(), to_be_written.size());
		int nb_calls = 1;
		std::cout << "written_bytes=" << len << " remaining=" << (to_be_written.size() - len) << "\n";

		while (len < to_be_written.size()) {
			auto offset = len;
			auto remaining = to_be_written.size() - len;
			len += write(fd, to_be_written.c_str() + offset, remaining);
			std::cout << "written_bytes=" << len << " remaining=" << (to_be_written.size() - len) << "\n";

			nb_calls++;
		}

		std::string tmp_str = "write() x" + std::to_string(nb_calls);
		wait_for_enter(tmp_str);

#if 0
		auto l = lseek(fd, 0, SEEK_SET)  ;
		if (l < 0) {
			perror("Error lseek()");
			close(fd);
			return 1;
		}

		std::unique_ptr<char[]> tmp_buf = std::make_unique<char[]>(1024);
		int read_bytes = 0;
		int remaining_bytes = to_be_written.size();
		nb_calls = 0;
		do {
			read_bytes += read(fd, tmp_buf.get()+read_bytes, remaining_bytes);
			remaining_bytes = to_be_written.size() - read_bytes;
			nb_calls++;
			std::cout << "read_bytes=" << read_bytes << " remaining_bytes=" << remaining_bytes << "\n";
			wait_for_enter(" stopwatch ");
		} while (remaining_bytes > 0);

		tmp_str = "read() x" + std::to_string(nb_calls);
#endif
		wait_for_enter(tmp_str);

		if (fsync(fd) == -1) {
			perror("Error synchronizing file");
			close(fd);
			return 1;
		}
	}
	wait_for_enter("fsync()");
#endif
	close(fd);
//	wait_for_enter("close()");

	printf("File written successfully.\n");
	return 0;
}