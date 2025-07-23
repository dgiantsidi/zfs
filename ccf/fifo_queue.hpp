#include "config_c.h"
#include <mutex>
#include <vector>
// #include <queue>
#include <stack>

template <typename T> class fifo_queue {
  //    std::queue<T> queue;

  std::stack<T> lifo;

  std::mutex mtx;

public:
  fifo_queue() = default;

  ~fifo_queue() {
    if (lifo.size() > 0) {
      printf(
          "fifo_queue: destructor called, but queue is not empty, size=%lu\n",
          lifo.size());
    }
  }

  void push(T msg) {
    std::lock_guard<std::mutex> lock(mtx);
    lifo.push(msg);
  }

  bool empty() {
    std::lock_guard<std::mutex> lock(mtx);
    return lifo.empty();
  }

  T pop() {
    std::lock_guard<std::mutex> lock(mtx);
    if (!lifo.empty()) {
      T msg = lifo.top();
      lifo.pop();
      return msg;
    }
    return nullptr;
  }

  std::vector<T> pop_until_blk_id(uint64_t blk_id) {
    std::vector<T> ret_vec;
    std::lock_guard<std::mutex> lock(mtx);
    while (!lifo.empty()) {
      T msg = lifo.top();
      if (msg->blk_id <= blk_id) {
        ret_vec.emplace_back(msg);
        lifo.pop();
      } else {
        return ret_vec;
      }
    }
    return ret_vec;
  }
};