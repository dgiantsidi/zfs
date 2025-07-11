#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zil.h>
#include <sys/map.h>

extern C_map_t cksum_map;
extern C_map_t recovery_map;
extern kmutex_t my_mutex;

