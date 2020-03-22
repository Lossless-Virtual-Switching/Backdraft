/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct params {
  uint32_t conns;
  uint32_t bytes;
  uint32_t ip;
  uint32_t threads;
  uint32_t tx_delay;
  uint32_t op_delay;
  int tx;
  uint16_t port;
};

static inline int parse_params(int argc, char *argv[], struct params *p)
{
  static const char *short_opts = "i:p:t:c:b:d:o:r";
  int c, done = 0;
  char *end;

  p->conns = 1;
  p->bytes = 1;
  p->ip = 0;
  p->threads = 1;
  p->tx = 1;
  p->port = 1234;
  p->tx_delay = 0;

  while (!done) {
    c = getopt(argc, argv, short_opts);
    switch (c) {
      case 'i':
        if ((p->ip = inet_addr(optarg)) == INADDR_NONE) {
          fprintf(stderr, "parsing IP failed\n");
          return -1;
        }
        break;

      case 'p':
        p->port = strtoul(optarg, &end, 10);
        if (!*optarg || *end || p->port < 1) {
          fprintf(stderr, "port needs to be a positive integer\n");
          return -1;
        }
        break;

      case 't':
        p->threads = strtoul(optarg, &end, 10);
        if (!*optarg || *end || p->threads < 1) {
          fprintf(stderr, "threads needs to be a positive integer\n");
          return -1;
        }
        break;

      case 'c':
        p->conns = strtoul(optarg, &end, 10);
        if (!*optarg || *end || p->conns < 1) {
          fprintf(stderr, "conns needs to be a positive integer\n");
          return -1;
        }
        break;

      case 'b':
        p->bytes = strtoul(optarg, &end, 10);
        if (!*optarg || *end || p->bytes < 1) {
          fprintf(stderr, "bytes needs to be a positive integer\n");
          return -1;
        }
        break;

      case 'd':
        p->tx_delay = strtoul(optarg, &end, 10);
        if (!*optarg || *end) {
          fprintf(stderr, "bytes needs to be an integer\n");
          return -1;
        }
        break;

      case 'o':
        p->op_delay = strtoul(optarg, &end, 10);
        if (!*optarg || *end) {
          fprintf(stderr, "op delay needs to be an integer\n");
          return -1;
        }
        break;

      case 'r':
        p->tx = 0;
        break;

      case -1:
        done = 1;
        break;

      case '?':
        goto failed;

      default:
        abort();
    }
  }
  return 0;

failed:
  fprintf(stderr,
      "Usage: %s OPTIONS\n"
      "Options:\n"
      "  -i IP       IP address for client mode       [default server mode]\n"
      "  -p PORT     Port to listen on/connect to     [default 1234]\n"
      "  -t THREADS  Number of threads to use         [default 1]\n"
      "  -c CONNS    Number of connections per thread [default 1]\n"
      "  -b BYTES    Message size in bytes            [default 1]\n"
      "  -r          Receive mode                     [default transmit]\n"
      "  -d DELAY    Seconds before sending starts    [default 0]\n"
      "  -o DELAY    Cycles to artificially delay/op  [default 0]\n",
      argv[0]);
  return -1;
}
