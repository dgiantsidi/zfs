#include "config.h"

[[maybe_unused]] static char *
serialize_notify_cmt_msg(const notify_cmt_msg_t *msg) {
  char *serialized_msg = (char *)malloc(sizeof(notify_cmt_msg_t));
  memcpy(serialized_msg, &(msg->acknowledged_blk_id),
         sizeof(msg->acknowledged_blk_id));
  memcpy(serialized_msg + sizeof(msg->acknowledged_blk_id), msg->poolname,
         ZFS_MAX_DATASET_NAME_LEN);
  return serialized_msg;
}

[[maybe_unused]] static char *
serialize_notify_cmt_into_char(const char *poolname, uint64_t blk_id) {
  char *serialized_msg = (char *)malloc(sizeof(notify_cmt_msg_t));
  memcpy(serialized_msg, &blk_id, sizeof(blk_id));
  memcpy(serialized_msg + sizeof(blk_id), poolname, ZFS_MAX_DATASET_NAME_LEN);
  return serialized_msg;
}

[[maybe_unused]] static char *serialize_get_cmt_msg(const get_cmt_msg_t *msg) {
  char *serialized_msg = (char *)malloc(sizeof(get_cmt_msg_t));
  memcpy(serialized_msg, msg->poolname, ZFS_MAX_DATASET_NAME_LEN);
  return serialized_msg;
}

[[maybe_unused]] static char *
serialize_get_cmt_into_char(const char *poolname) {
  char *serialized_msg = (char *)malloc(sizeof(get_cmt_msg_t));
  memcpy(serialized_msg, poolname, ZFS_MAX_DATASET_NAME_LEN);
  return serialized_msg;
}

[[maybe_unused]] static recv_cmt_msg_t *deserialize_recv_cmt(const char *msg) {
  recv_cmt_msg_t *recv_msg = (recv_cmt_msg_t *)malloc(sizeof(recv_cmt_msg_t));
  memcpy(&(recv_msg->blk_id), msg, sizeof(recv_msg->blk_id));
  memcpy(&(recv_msg->poolname), msg + sizeof(recv_msg->blk_id),
         ZFS_MAX_DATASET_NAME_LEN);
  memcpy(&(recv_msg->tail_commitment),
         msg + sizeof(recv_msg->blk_id) + ZFS_MAX_DATASET_NAME_LEN,
         COMMITMENT_SIZE);
  return recv_msg;
}
