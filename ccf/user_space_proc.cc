#include "config_c.h"
#include "fifo_queue.hpp"
#include "msg_processing_functions.h"
#include <atomic>
#include <condition_variable>
#include <errno.h>
#include <linux/netlink.h>
#include <mutex>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <thread>
#include <time.h>
#include <unistd.h>

using u_longlong_t = unsigned long long;

fifo_queue<recv_cmt_msg_t *> recv_queue; // queue to store received messages
std::atomic<int> get_thread_done(false);
static uint64_t c_total_ops = 100e6;
std::condition_variable ccf_thread_cv;
std::mutex ccf_thread_mutex;
std::unique_ptr<char[]> latest_head_ub_commitment(nullptr);
std::atomic<uint64_t> latest_txg(0);
std::mutex global_head_cmt;
bool k_print_cmts = false;

static inline uint64_t get_current_time_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void *notify_ubcmts(void *arg_poolname) {

  struct sockaddr_nl src_addr, dest_addr;
  struct msghdr msg;
  struct iovec iov;
  [[maybe_unused]] const char *poolname = (const char *)arg_poolname;
  uint64_t total_ops = 0;

  // create socket for sending notify messages
  int sock_fd = socket(PF_NETLINK, SOCK_RAW, NOTIFY_UBCMTS_SOCK);
  if (sock_fd < 0) {
    printf("error creating the socket of type=%s, errno: %s\n",
           get_socket_type(NOTIFY_UBCMTS_SOCK), strerror(errno));
    return NULL;
  }

  memset(&src_addr, 0, sizeof(src_addr));
  src_addr.nl_family = AF_NETLINK;
  src_addr.nl_pid = getpid(); /* self pid */
  src_addr.nl_groups = 0;     /* not in mcast groups */
  if (bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr)) < 0) {
    printf("error binding the socket of type=NOTIFY_UBCMTS_SOCK, errno: %s\n",
           strerror(errno));
    close(sock_fd);
    return NULL;
  }
  //sleep(15);

  struct timespec start, end;
  uint64_t acknowledged_txg_ub = 0;
  for (;;) {
    while (latest_txg.load() == 0) {
      // waiting for the first commitment to be generated
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    while (acknowledged_txg_ub == latest_txg.load()) {
      // we avoid using the >= to enable notifying uberblock thread when we destroy and re-create the pool
      // waiting for a new commitment to be generated
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::unique_ptr<char[]> recv_msg = std::make_unique<char[]>(512);
    {
      std::lock_guard<std::mutex> tmp_lock(global_head_cmt);
      ::memcpy(recv_msg.get(), latest_head_ub_commitment.get(), 512);
    } 
    uint64_t ub_txg = 0;
    ::memcpy(&ub_txg, recv_msg.get(), sizeof(ub_txg));
    acknowledged_txg_ub = ub_txg;
      
      

    // get start time
    struct nlmsghdr *nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(512));
     memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;    /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

    /* fill the netlink message header */
    nlh->nlmsg_len = NLMSG_SPACE(512);
    nlh->nlmsg_pid = getpid(); /* self pid */
    nlh->nlmsg_flags = 0;

    char buf[512];
    memset(buf, 0, 512);

    /* fill in the netlink message payload */
    memcpy(NLMSG_DATA(nlh), buf, 512);

    memset(&iov, 0, sizeof(iov));
    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    int rc = sendmsg(sock_fd, &msg, 0);
    if (rc < 0) {
      printf("error seding the message: %s\n", strerror(errno));
      close(sock_fd);
      return NULL;
    }
    printf("notify_ubcmts: notification sent already to kernel about new uberblock commitment for ub_txg=%lu\n", ub_txg);
    total_ops++;
    free(nlh);
    //sleep(5);
  }
  // get end time
  clock_gettime(CLOCK_MONOTONIC, &end);

  // calculate elapsed time in seconds
  long long elapsed_ns =
      (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  double latency_us = (elapsed_ns / 1e3) / total_ops; // convert to microseconds
  printf("elapsed time: %llu  nanoseconds (latency per operation = %f us, "
         "total_ops=%lu), "
         "msg_size=%lu\n",
         elapsed_ns, latency_us, total_ops, sizeof(notify_cmt_msg_t));

  /* close Netlink Socket */
  close(sock_fd);
  return NULL;
}

static void *notify_cmts(void *arg_poolname) {
  static uint64_t sum_latency_ns = 0;
  static uint64_t count_latency = 0;
  struct sockaddr_nl src_addr, dest_addr;
  static uint64_t last_acked_blk_id = 0;
  struct msghdr msg;
  struct iovec iov;
  [[maybe_unused]] const char *poolname = (const char *)arg_poolname;
  uint64_t total_ops = 0;

  // create socket for sending notify messages
  int sock_fd = socket(PF_NETLINK, SOCK_RAW, NOTIFY_CMTS_SOCK);
  if (sock_fd < 0) {
    printf("error creating the socket of type=%s, errno: %s\n",
           get_socket_type(NOTIFY_CMTS_SOCK), strerror(errno));
    return NULL;
  }

  memset(&src_addr, 0, sizeof(src_addr));
  src_addr.nl_family = AF_NETLINK;
  src_addr.nl_pid = getpid(); /* self pid */
  src_addr.nl_groups = 0;     /* not in mcast groups */
  bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr));

  struct timespec start, end;

  // get start time
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (;;) {
    if (get_thread_done.load() && recv_queue.empty()) {
      printf("notify_cmts: get_thread_done is true and recv_queue is empty, "
             "exiting...\n");
      break;
    }

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;    /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

    // randomized_sleeps();
    recv_cmt_msg_t *last_cmt = recv_queue.pop();
    while ((last_cmt == nullptr)) {
      // std::unique_lock<std::mutex> lock(ccf_thread_mutex);
      // ccf_thread_cv.wait(lock);
      last_cmt = recv_queue.pop();
    }

    sum_latency_ns += get_current_time_ns() - last_cmt->timestamp_ns;
    count_latency++;
    struct nlmsghdr *nlh =
        (struct nlmsghdr *)malloc(NLMSG_SPACE(sizeof(notify_cmt_msg_t)));

    /* fill the netlink message header */
    nlh->nlmsg_len = NLMSG_SPACE(sizeof(notify_cmt_msg_t));
    nlh->nlmsg_pid = getpid(); /* self pid */
    nlh->nlmsg_flags = 0;

    char *tx_msg =
        serialize_notify_cmt_into_char(last_cmt->poolname, last_cmt->blk_id);
    uint64_t last_blk_id = last_cmt->blk_id;

    /* fill in the netlink message payload */
    memcpy(NLMSG_DATA(nlh), tx_msg, sizeof(notify_cmt_msg_t));

    memset(&iov, 0, sizeof(iov));
    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    uint64_t blk_id = 0;
    memcpy(&blk_id, tx_msg, sizeof(uint64_t));
#if 0
    printf("%s send to kernel: {%ld, %dB}\n", __func__, blk_id, nlh->nlmsg_len)
#endif
  if (k_print_cmts) {
    uint64_t tail_digest[4];
    ::memcpy(tail_digest, last_cmt->tail_commitment, sizeof(tail_digest));

    printf("%s for blk=%016x (%ld)}\n", __func__, blk_id, blk_id); 
    
    printf("%016llx:%016llx:%016llx:%016llx\n", (u_longlong_t)tail_digest[0],\
    (u_longlong_t)tail_digest[1], (u_longlong_t)tail_digest[2], (u_longlong_t)tail_digest[3]);
  }

    int rc = sendmsg(sock_fd, &msg, 0);
    if (rc < 0) {
      printf("error seding the message: %s\n", strerror(errno));
      close(sock_fd);
      return NULL;
    }
    total_ops++;
    free(nlh);
    free(tx_msg);
    free(last_cmt);

    last_acked_blk_id = last_blk_id;
    std::vector<recv_cmt_msg_t *> to_be_deleted =
        recv_queue.pop_until_blk_id(last_acked_blk_id);
#if 0
    printf("delete about %ld entries from the queue with last_blk_id=%ld\n",
           to_be_deleted.size(), last_acked_blk_id);
#endif
    for (auto &buf : to_be_deleted) {
      free(buf); // free the messages that were popped from the queue
    }
    if (total_ops % 10000 == 0) {
      auto avg_latency_us = (sum_latency_ns * 1.0 / count_latency * 1.0) / 1e3;
      printf("notify_cmts: total_ops=%lu, last_acked_blk_id=%lu "
             "avg_latency=%.2f us\n",
             total_ops, last_acked_blk_id, avg_latency_us);
    }
    // printf("done with deletion \n", to_be_deleted.size());
  }

  // get end time
  clock_gettime(CLOCK_MONOTONIC, &end);

  // calculate elapsed time in seconds
  long long elapsed_ns =
      (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  double latency_us = (elapsed_ns / 1e3) / total_ops; // convert to microseconds
  printf("elapsed time: %llu  nanoseconds (latency per operation = %f us, "
         "total_ops=%lu), "
         "msg_size=%lu\n",
         elapsed_ns, latency_us, total_ops, sizeof(notify_cmt_msg_t));

  /* close Netlink Socket */
  close(sock_fd);
  return NULL;
}

static size_t max_buffer_size() {
  return (sizeof(get_cmt_msg_t) > sizeof(recv_cmt_msg_t))
             ? sizeof(get_cmt_msg_t)
             : sizeof(recv_cmt_msg_t);
}

static void *get_cmts(void *arg_poolname) {
  const char *poolname = (const char *)arg_poolname;
  static uint64_t expected_blk_id = 0; // static to retain value between calls
  struct sockaddr_nl src_addr, dest_addr;

  struct msghdr msg;
  struct iovec iov;

  int sock_fd = socket(PF_NETLINK, SOCK_RAW, GET_CMTS_SOCK);
  if (sock_fd < 0) {
    printf("error creating the socket of type=%s, errno: %s\n",
           get_socket_type(NOTIFY_CMTS_SOCK), strerror(errno));
    return NULL;
  }

  memset(&src_addr, 0, sizeof(src_addr));
  src_addr.nl_family = AF_NETLINK;
  src_addr.nl_pid = getpid(); /* self pid */
  src_addr.nl_groups = 0;     /* not in mcast groups */
  bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr));

  struct timespec start, end;

  // get start time
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (;;) {
    if (expected_blk_id == c_total_ops) {
      // break;
    }
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;    /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

    struct nlmsghdr *nlh =
        (struct nlmsghdr *)malloc(NLMSG_SPACE(max_buffer_size()));

    /* fill the netlink message header */
    nlh->nlmsg_len = NLMSG_SPACE(max_buffer_size());
    nlh->nlmsg_pid = getpid(); /* self pid */
    nlh->nlmsg_flags = 0;

    char *tx_msg = serialize_get_cmt_into_char(poolname);
    /* fill in the netlink message payload */
    memcpy(NLMSG_DATA(nlh), tx_msg, sizeof(get_cmt_msg_t));

    memset(&iov, 0, sizeof(iov));
    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

#if 0
    printf("%s send to kernel: {%s, %dB, pid=%d}\n", __func__, poolname,
           nlh->nlmsg_len, nlh->nlmsg_pid);
#endif
    free(tx_msg);
    int rc = sendmsg(sock_fd, &msg, 0);
    if (rc < 0) {
      printf("error seding the message: %s\n", strerror(errno));
      close(sock_fd);
      return NULL;
    }

    /* read message from kernel */
    memset(nlh, 0, NLMSG_SPACE(max_buffer_size()));

    rc = recvmsg(sock_fd, &msg, 0);
    if (rc < 0) {
      printf("sendmsg(): %s\n", strerror(errno));
      close(sock_fd);
      return NULL;
    }

    recv_cmt_msg_t *recv_msg =
        deserialize_recv_cmt(reinterpret_cast<char *>(NLMSG_DATA(nlh)));
    recv_msg->timestamp_ns = get_current_time_ns();

#if 0
    printf("received from kernel: {blk_id=%ld, %s, cmt=%s}\n", recv_msg->blk_id,
           recv_msg->poolname, recv_msg->tail_commitment);
#endif
    if (expected_blk_id % 10000 == 0) {
      printf("get_cmts: total_ops=%lu, last_blk_id=%lu\n", expected_blk_id,
             recv_msg->blk_id);
    }
    recv_queue.push(recv_msg); // push the received message to the queue
    // ccf_thread_cv.notify_one(); // wake up notify_cmts thread

    free(nlh);
    expected_blk_id++;
  }

  // get end time
  clock_gettime(CLOCK_MONOTONIC, &end);

  // calculate elapsed time in seconds
  long long elapsed_ns =
      (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  double latency_us =
      (elapsed_ns / 1e3) / c_total_ops; // convert to microseconds
  printf("elapsed time: %llu  nanoseconds (latency per operation = %f us), "
         "msg_size=%lu\n",
         elapsed_ns, latency_us, max_buffer_size());

  /* close Netlink Socket */
  close(sock_fd);
  return NULL;
}

static void *get_cmts_ub(void *arg_poolname) {
  [[__maybe_unused__]] const char *poolname = (const char *)arg_poolname;
  static uint64_t expected_blk_id = 0; // static to retain value between calls
  struct sockaddr_nl src_addr, dest_addr;
  char head_ub_commitment[512];
  ::memset(head_ub_commitment, 0, sizeof(head_ub_commitment));
  struct msghdr msg;
  struct iovec iov;
  uint64_t prev_ub_txg = 0;
  uint64_t prev_zil_head_blk_num = -1;

  int sock_fd = socket(PF_NETLINK, SOCK_RAW, GET_UBCMTS_SOCK);
  if (sock_fd < 0) {
    printf("error creating the socket of type=GET_UBCMTS_SOCK, errno: %s\n",
           strerror(errno));
    return NULL;
  }

  memset(&src_addr, 0, sizeof(src_addr));
  src_addr.nl_family = AF_NETLINK;
  src_addr.nl_pid = getpid(); /* self pid */
  src_addr.nl_groups = 0;     /* not in mcast groups */
  bind(sock_fd, (struct sockaddr *)&src_addr, sizeof(src_addr));

  struct timespec start, end;

  // get start time
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (;;) {
    if (expected_blk_id == c_total_ops) {
      // break;
    }
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0;    /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

    struct nlmsghdr *nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(512));

    /* fill the netlink message header */
    nlh->nlmsg_len = NLMSG_SPACE(512);
    nlh->nlmsg_pid = getpid(); /* self pid */
    nlh->nlmsg_flags = 0;

    /* fill in the netlink message payload */
    memcpy(NLMSG_DATA(nlh), head_ub_commitment, 512);

    memset(&iov, 0, sizeof(iov));
    iov.iov_base = (void *)nlh;
    iov.iov_len = nlh->nlmsg_len;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *)&dest_addr;
    msg.msg_namelen = sizeof(dest_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

#if 0
    printf("%s send to kernel: {%s, %dB, pid=%d}\n", __func__, poolname,
           nlh->nlmsg_len, nlh->nlmsg_pid);
#endif
    int rc = sendmsg(sock_fd, &msg, 0);
    if (rc < 0) {
      printf("error seding the message: %s\n", strerror(errno));
      close(sock_fd);
      return NULL;
    }

    /* read message from kernel */
    memset(nlh, 0, NLMSG_SPACE(512));

    rc = recvmsg(sock_fd, &msg, 0);
    if (rc < 0) {
      printf("sendmsg(): %s\n", strerror(errno));
      close(sock_fd);
      return NULL;
    }

    std::unique_ptr<char[]> recv_msg = std::make_unique<char[]>(512);
    ::memcpy(recv_msg.get(), reinterpret_cast<char *>(NLMSG_DATA(nlh)), 512);
    size_t offset = 0;
    uint64_t head_digest[4];
    uint64_t ub_txg = 0, zil_head_blk_num = 0;
    memcpy(&ub_txg, recv_msg.get(), sizeof(ub_txg));
    offset += sizeof(ub_txg);
    if (ub_txg != prev_ub_txg)
      printf("ub_txg=%llu, offset=%llu\n", (u_longlong_t) ub_txg, (u_longlong_t) offset);
    offset += 65;
    memcpy(&zil_head_blk_num, recv_msg.get() + offset, sizeof(zil_head_blk_num));
    offset += sizeof(zil_head_blk_num);
    if (zil_head_blk_num != prev_zil_head_blk_num)
      printf("zil_head_blk_num=%llu (%016llx), offset=%llu\n", (u_longlong_t) zil_head_blk_num,
           (u_longlong_t) zil_head_blk_num, (u_longlong_t) offset);
    memcpy(head_digest, recv_msg.get() + offset, sizeof(head_digest));
    offset += sizeof(head_digest);
    if (zil_head_blk_num != prev_zil_head_blk_num)
      printf("head_digest=%016llx:%016llx:%016llx:%016llx\n", (u_longlong_t) head_digest[0],
           (u_longlong_t) head_digest[1], (u_longlong_t) head_digest[2], (u_longlong_t) head_digest[3]);

    if (ub_txg != prev_ub_txg || zil_head_blk_num != prev_zil_head_blk_num)
    {
      latest_txg.store(ub_txg);
      global_head_cmt.lock();
      latest_head_ub_commitment = std::move(recv_msg);
      global_head_cmt.unlock();
    }
    prev_ub_txg = ub_txg;
    prev_zil_head_blk_num = zil_head_blk_num;
    // recv_queue.push(recv_msg); // push the received message to the queue

    usleep(1000); // 
    free(nlh);
  }

  // get end time
  clock_gettime(CLOCK_MONOTONIC, &end);

  // calculate elapsed time in seconds
  long long elapsed_ns =
      (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  double latency_us =
      (elapsed_ns / 1e3) / c_total_ops; // convert to microseconds
  printf("elapsed time: %llu  nanoseconds (latency per operation = %f us), "
         "msg_size=%lu\n",
         elapsed_ns, latency_us, max_buffer_size());

  /* close Netlink Socket */
  close(sock_fd);
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t get_cmts_thread, notify_cmts_thread, get_ubcmts_thread, notify_ubcmts_thread;

  if (argc == 1) {
    fprintf(stderr, "Usage: %s <poolname> [<threads> <enable_print>]\n", argv[0]);
    return 1;
  }
  char *poolname = argv[1];
  printf("poolname: %s\n", poolname);
  if (argc >= 3) {
    k_print_cmts = atoi(argv[2]) != 0;
  }

  // create two threads
  if (pthread_create(&get_cmts_thread, NULL, get_cmts, poolname) != 0) {
    perror("failed to create get_cmts_thread");
    return 1;
  }

  if (pthread_create(&notify_cmts_thread, NULL, notify_cmts, poolname) != 0) {
    perror("failed to create notify_cmts_thread");
    return 1;
  }
#if 1
  if (pthread_create(&get_ubcmts_thread, NULL, get_cmts_ub, poolname) != 0) {
    perror("failed to create get_ubcmts_thread");
    return 1;
  }

  if (pthread_create(&notify_ubcmts_thread, NULL, notify_ubcmts, poolname) !=
      0) {
    perror("failed to create notify_ubcmts_thread");
    return 1;
  }
#endif
  // wait for both threads to finish
  pthread_join(get_cmts_thread, NULL);
  get_thread_done.store(true);
  pthread_join(notify_cmts_thread, NULL);
  pthread_join(get_ubcmts_thread, NULL);
  pthread_join(notify_ubcmts_thread, NULL);

  printf("both threads have completed.\n");
  return 0;
}