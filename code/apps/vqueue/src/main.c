#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include "app.h"

#define CLIENT 1
#define SERVER 2

static int dpdk_init(int argc, char *argv[])
{
  int arg_parsed;
  arg_parsed = rte_eal_init(argc, argv);
  if (arg_parsed < 0)
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
  return arg_parsed;
}

static void print_usage(void)
{
  printf("usage: mode\n");
  printf("* mode: client or server\n");
}

int main(int argc, char *argv[])
{
  int arg_dpdk;
  int mode;

  arg_dpdk = dpdk_init(argc, argv);
  argc -= arg_dpdk;
  argv += arg_dpdk;

  if (argc < 2) {
    print_usage();
    return -1;
  }

  if (strcmp(argv[1], "client") == 0) {
    mode = CLIENT;
  } else if (strcmp(argv[1], "server") == 0) {
    mode = SERVER;
  } else {
    print_usage();
    return -1;
  }

  argv[1] = argv[0];
  argc -= 1;
  argv += 1;

  if (mode == SERVER) {
    server_main(argc, argv);
  } else {
    client_main(argc, argv);
  }
  return 0;
}
