#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "plugin_api.h"

static char *g_lib_name = "libaerN3253.so.0.0.2";

static char *g_plugin_purpose =
    "Search for files containing a given IPv4 address value either in text "
    "form(x.x.x.x), or binary(little-endian or big- endian)";

static char *g_plugin_author = "Arina Rastvortseva";

#define OPT_IPV4_ADDR "ipv4-addr"

static struct plugin_option g_po_arr[] = {

    {{
         OPT_IPV4_ADDR,
         required_argument,
         0,
         0,
     },
     "Returns 0 if the value was found in the file."}

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

int ret;
char *ptr = NULL;
int fd = 0;
char *val_copy = NULL;
char *val_copy_copy = NULL;

int plugin_process_file(const char *fname, struct option in_opts[],
                        size_t in_opts_len) {

  ret = 1;

  char *DEBUG = getenv("LAB1DEBUG");

  if (DEBUG) {
    for (size_t i = 0; i < in_opts_len; i++) {
      fprintf(stderr, "\nDEBUG: %s: Got option '%s' with arg '%s'\n",
              g_lib_name, in_opts[i].name, (char *)in_opts[i].flag);
    }
  }
  int saved_errno = 0;

  const char *val = (char *)in_opts[0].flag;

  //валидация iPv4:

  in_addr_t ip = inet_addr(val);

  if (ip == INADDR_NONE) {
    if (DEBUG) {
      fprintf(stderr, "DEBUG: %s: invalid argument value: %s\n", g_lib_name,
              val);
    }
    goto END;
  }

  fd = open(fname, O_RDONLY);
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

  if (DEBUG) {

    char start_esc[10] = {0}, end_esc[10] = {0};

    sprintf(start_esc, "%s", "\33[35m");
    sprintf(end_esc, "%s", "\33[0m");

    fprintf(stderr, "DEBUG: %s: File: %s%s%s\n", g_lib_name, start_esc, fname,
            end_esc);
  }

  char *str = strstr(ptr, val);
  if (str) {
    ret = 0;
  }
  // bin
  val_copy = strdup(val);
  val_copy_copy = val_copy;

  unsigned int big = 0;
  unsigned int little = 0;
  unsigned int octet = 0;

  for (int i = 0; i < 4; i++) {
    big <<= 8;
    octet = strtoul(val_copy, &val_copy, 10);
    big |= octet;
    octet <<= i * 8;
    little |= octet;

    val_copy++;
  }

  int test = 0;

  for (__off_t i = 0; i < st.st_size - 3; i++) {
    test ^= test;
    memcpy(&test, ptr + i, 4);

    if ((big ^ test) == 0 || (little ^ test) == 0) {
      ret = 0;

      if (DEBUG) {
        fprintf(stdout, "DEBUG: %s: the value was found at position: %ld \n",
                g_lib_name, i);
      }

      goto END;
    }
  }

END:

  close(fd);
  if (ptr != MAP_FAILED && ptr != NULL)
    munmap(ptr, st.st_size);

  // Restore errno value
  errno = saved_errno;

  if (val_copy_copy)
    free(val_copy_copy);
  val_copy = NULL;

  return ret;
}
