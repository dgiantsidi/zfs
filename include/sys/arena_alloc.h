#pragma once
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zil.h>
#include <sys/abd.h>
#include <zfs_fletcher.h>
#ifdef USERSPACE

void *alloc_node(size_t sz) { return malloc(sz); }

void free_node(void *ptr, size_t sz) {
  (void)sz;
  return free(ptr);
}
#endif
// #if 0

// #include <sys/kmem.h>

// #include <linux/sched.h>
//  #include <linux/vmalloc.h>

// function declarations
__attribute__((unused)) static void *alloc_node(size_t sz);
__attribute__((unused)) static void free_node(void *ptr, size_t sz);
__attribute__((unused)) static void _printf(const char *format, ...);

// function definitions
__attribute__((unused)) static void *alloc_node(size_t sz) {
  // return vmalloc(sz);
  // return kmem_alloc(sz, KM_SLEEP);
  return vmem_alloc(sz, KM_SLEEP);
}

__attribute__((unused)) static void free_node(void *ptr, size_t sz) { vmem_free(ptr, sz); } //kmem_free(ptr, sz); }

__attribute__((unused)) static void _printf(const char *format, ...) {
  va_list args;
  va_start(args, format);

  // add custom prefix
  zfs_dbgmsg("");

  // forward the arguments to vprintf
  zfs_dbgmsg(format, args);

  va_end(args);
}

// #endif