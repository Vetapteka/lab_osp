#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "plugin_api.h"

static char *g_lib_name = "libsaaN3251.so";

static char *g_plugin_purpose = "Calculates the checksum. ";

static char *g_plugin_author = "Artemenko Svetlana";

unsigned short crc16(unsigned char *, unsigned short);

#define OPT_CRC16 "crc16"

static struct plugin_option g_po_arr[] = {

    {{
         OPT_CRC16,
         required_argument,
         0,
         0,
     },
     "Returns 0 if the sum is equal to the entered value."}

};

static int g_po_arr_len = sizeof(g_po_arr) / sizeof(g_po_arr[0]);

int plugin_get_info(struct plugin_info *ppi) {
  if (!ppi) {
    fprintf(stderr, "ERROR: invalid argument\n");
    return -1;
  }

  ppi->plugin_purpose = g_plugin_purpose;
  ppi->plugin_author = g_plugin_author;
  ppi->sup_opts_len = g_po_arr_len;
  ppi->sup_opts = g_po_arr;

  return 0;
}

unsigned char *ptr = NULL;
int ret;

int plugin_process_file(const char *fname, struct option in_opts[],
                        size_t in_opts_len) {
  ret = -1;

  char *DEBUG = getenv("LAB1DEBUG");

  if (DEBUG) {
    for (size_t i = 0; i < in_opts_len; i++) {
      fprintf(stderr, "DEBUG: %s: Got option '%s' with arg '%s'\n", g_lib_name,
              in_opts[i].name, (char *)in_opts[i].flag);
    }
  }
  int saved_errno = 0;

  char *ptr_endptr = NULL;
  int res_client;
  char *val = (char *)in_opts[0].flag;

  res_client = strtol(val, &ptr_endptr, 10);

  if (*ptr_endptr) {
    if (strstr(val, "0x"))
      res_client = strtol(val + 2, &ptr_endptr, 16);
    if (strstr(val, "0b"))
      res_client = strtol(val + 2, &ptr_endptr, 2);
  }
  if (*ptr_endptr) {
    if (DEBUG)
      fprintf(stderr, "DEBUG: %s: Failed to convert '%s'\n", g_lib_name, val);

    errno = EINVAL;
    return -1;
  }

  int fd = open(fname, O_RDONLY);
  if (fd < 0) {
    // errno is set by open()
    return -1;
  }

  struct stat st = {0};
  int res = fstat(fd, &st);
  if (res < 0) {
    saved_errno = errno;
    goto END;
  }

  // Check that size of file is > 0
  if (st.st_size == 0) {
    if (DEBUG) {
      fprintf(stderr, "DEBUG: %s: File size should be > 0\n", g_lib_name);
    }
    saved_errno = ERANGE;
    goto END;
  }

  ptr = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (ptr == MAP_FAILED) {
    saved_errno = errno;
    goto END;
  }

  unsigned short calc_crc16 = crc16(ptr, st.st_size);

  if (DEBUG) {
    fprintf(stdout, "\nDEBUG: %s: file: %s\n", g_lib_name, fname);
    fprintf(stdout, "DEBUG: %s: result of crc16: %d\n", g_lib_name, calc_crc16);
  }

  // 0  - file is ok
  // 1  - file isn't ok
  ret = calc_crc16 != res_client;

END:

  close(fd);
  if (ptr != MAP_FAILED && ptr != NULL)
    munmap(ptr, st.st_size);

  // Restore errno value
  errno = saved_errno;

  return ret;
}

/*
  Name  : CRC-16 CCITT
  Poly  : 0x1021    x^16 + x^12 + x^5 + 1
  Init  : 0xFFFF
  Revert: false
  XorOut: 0x0000
  Check : 0x29B1 ("123456789")
  MaxLen: 4095 байт (32767 бит) - обнаружение
    одинарных, двойных, тройных и всех нечетных ошибок
*/

unsigned short crc16(unsigned char *pcBlock, unsigned short len) {
  unsigned short crc = 0xFFFF;
  unsigned char i;

  while (len--) {
    crc ^= *pcBlock++ << 8;

    for (i = 0; i < 8; i++)
      crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}