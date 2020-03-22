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

#ifdef USE_DPDK
#define ETHARP_HWADDR_LEN     6

struct eth_addr {
  uint8_t addr[ETHARP_HWADDR_LEN];
} __attribute__ ((packed));
#endif

struct worker {
  const char		*hostname;
#ifdef USE_DPDK
  struct eth_addr	mac;
  uint32_t		seq;
#endif
#ifdef USE_MTCP
  mctx_t		_mtcp_ctx;
#endif
  uint16_t		port;
  int			sock;
  struct executor 	executors[MAX_EXECUTORS];
};

// All bolts
void print_execute(const struct tuple *t, struct executor *self);
void rank_execute(const struct tuple *t, struct executor *self);
void count_execute(const struct tuple *t, struct executor *self);
void spout_execute(const struct tuple *t, struct executor *self);

void spout_init(struct executor *self);
void count_init(struct executor *self);

__attribute__ ((unused))
static int fields_grouping(const struct tuple *t, struct executor *self)
{
  static __thread int numtasks = 0;

  if(numtasks == 0) {
    // Remember number of tasks
    for(numtasks = 0; numtasks < MAX_TASKS; numtasks++) {
      if(self->outtasks[numtasks] == 0) {
	break;
      }
    }
    assert(numtasks > 0);
  }

  return self->outtasks[hash(t->v[0].str, strlen(t->v[0].str), 0) % numtasks];
}

__attribute__ ((unused))
static int global_grouping(const struct tuple *t, struct executor *self)
{
  return self->outtasks[0];
}

__attribute__ ((unused))
static int shuffle_grouping(const struct tuple *t, struct executor *self)
{
  static __thread int numtasks = 0;

  if(numtasks == 0) {
    // Remember number of tasks
    for(numtasks = 0; numtasks < MAX_TASKS; numtasks++) {
      if(self->outtasks[numtasks] == 0) {
	break;
      }
    }
    assert(numtasks > 0);
  }

  return self->outtasks[random() % numtasks];
}

static struct worker workers[MAX_WORKERS] = {
#if defined(LOCAL)
  {
    .hostname = "127.0.0.1", .port = 7001,
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 1, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 2, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 3, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 4, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
    }
  },
  {
    .hostname = "127.0.0.1", .port = 7002,
    .executors = {
      { .execute = count_execute, .init = count_init, .taskid = 10, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 11, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 12, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 13, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
    }
  },
  {
    .hostname = "127.0.0.1", .port = 7003,
    .executors = {
      { .execute = rank_execute, .taskid = 20, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 21, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 22, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 23, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "127.0.0.1", .port = 7004,
    .executors = {
      { .execute = rank_execute, .taskid = 30, .outtasks = { 0 }, .grouper = global_grouping },
      /* { .execute = print_execute, .taskid = 40 }, */
    }
  },
#elif defined(BIGFISH)
  {
    .hostname = "128.208.6.106", .port = 7001,
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 3, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 12, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 20, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 23, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "128.208.6.106", .port = 7002,
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 1, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 4, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 11, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 13, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 21, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "128.208.6.106", .port = 7003,
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 2, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 10, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 22, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 30, .outtasks = { 0 }, .grouper = global_grouping },
    }
  },
#elif defined(BIGFISH_FLEXNIC)
  {
    /* .hostname = "128.208.6.106", .port = 7001, */
    .hostname = "192.168.26.22", .port = 7001,
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 3, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 12, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 20, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 23, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    /* .hostname = "128.208.6.106", .port = 7002, */
    .hostname = "192.168.26.22", .port = 7002,
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 1, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 4, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 11, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 13, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 21, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    /* .hostname = "128.208.6.106", .port = 7003, */
    .hostname = "192.168.26.22", .port = 7003,
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 2, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 10, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 22, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 30, .outtasks = { 0 }, .grouper = global_grouping },
    }
  },
#elif defined(BIGFISH_FLEXNIC_DPDK)
  {
    .hostname = "128.208.6.236", .port = 7001, .mac.addr = "\xa0\x36\x9f\x0f\xfb\xe0",	// swingout3
    /* .hostname = "192.168.26.8", .port = 7001, .mac.addr = "\xa0\x36\x9f\x0f\xfb\xe0",	// swingout3 */
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 3, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 12, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 20, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 23, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "128.208.6.106", .port = 7002, .mac.addr = "\xa0\x36\x9f\x10\x03\x20",	// bigfish
    /* .hostname = "192.168.26.22", .port = 7002, .mac.addr = "\xa0\x36\x9f\x10\x03\x20",	// bigfish */
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 1, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 4, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 11, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 13, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 21, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "128.208.6.130", .port = 7003, .mac.addr = "\xa0\x36\x9f\x10\x00\xa0",	// swingout5
    /* .hostname = "192.168.26.20", .port = 7003, .mac.addr = "\xa0\x36\x9f\x10\x00\xa0",	// swingout5 */
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 2, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 10, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 22, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 30, .outtasks = { 0 }, .grouper = global_grouping },
    }
  },
#elif defined(BIGFISH_FLEXNIC_DPDK2)
  {
    .hostname = "128.208.6.236", .port = 7001, .mac.addr = "\xa0\x36\x9f\x0f\xfb\xe0",	// swingout3
    /* .hostname = "192.168.26.8", .port = 7001, .mac.addr = "\xa0\x36\x9f\x0f\xfb\xe0",	// swingout3 */
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 1, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 2, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 20, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 21, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "128.208.6.106", .port = 7002, .mac.addr = "\xa0\x36\x9f\x10\x03\x20",	// bigfish
    /* .hostname = "192.168.26.22", .port = 7002, .mac.addr = "\xa0\x36\x9f\x10\x03\x20",	// bigfish */
    .executors = {
      { .execute = count_execute, .init = count_init, .taskid = 10, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 11, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 12, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 13, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
    }
  },
  {
    .hostname = "128.208.6.130", .port = 7003, .mac.addr = "\xa0\x36\x9f\x10\x00\xa0",	// swingout5
    /* .hostname = "192.168.26.20", .port = 7003, .mac.addr = "\xa0\x36\x9f\x10\x00\xa0",	// swingout5 */
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 3, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 4, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 22, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 23, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 30, .outtasks = { 0 }, .grouper = global_grouping },
    }
  },
#elif defined(SWINGOUT_BALANCED)
  {
    .hostname = "10.0.0.1", .port = 7001,	// swingout1-brcm1
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 3, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 12, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 20, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 23, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "10.0.0.4", .port = 7002,	// swingout4
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 1, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 4, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 11, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 13, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 21, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "10.0.0.5", .port = 7003,	// swingout5
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 2, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 10, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 22, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 30, .outtasks = { 0 }, .grouper = global_grouping },
    }
  },
  /* { */
  /*   .hostname = "128.208.6.236", .port = 7004,	// swingout3 */
  /*   .executors = { */
  /*     /\* { .execute = print_execute, .taskid = 40 }, *\/ */
  /*   } */
  /* }, */
#elif defined(SWINGOUT_FLEXTCP_BALANCED)
  {
    .hostname = "128.208.6.67", .port = 7001,	// swingout1-brcm1
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 3, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 12, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 20, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 23, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "128.208.6.149", .port = 7002,	// swingout4
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 1, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 4, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 11, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 13, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 21, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "128.208.6.128", .port = 7003,	// swingout5
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 2, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 10, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 22, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 30, .outtasks = { 0 }, .grouper = global_grouping },
    }
  },
  /* { */
  /*   .hostname = "128.208.6.236", .port = 7004,	// swingout3 */
  /*   .executors = { */
  /*     /\* { .execute = print_execute, .taskid = 40 }, *\/ */
  /*   } */
  /* }, */
#elif defined(SWINGOUT_MTCP_BALANCED)
  {
    .hostname = "10.0.0.4", .port = 7002,	// swingout4
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 3, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 12, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 20, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 23, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "10.0.0.1", .port = 7002,	// swingout1-brcm1
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 1, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 4, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 11, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 13, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 21, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "10.0.0.5", .port = 7003,	// swingout5
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 2, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 10, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = rank_execute, .taskid = 22, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 30, .outtasks = { 0 }, .grouper = global_grouping },
    }
  },
  /* { */
  /*   .hostname = "128.208.6.236", .port = 7004,	// swingout3 */
  /*   .executors = { */
  /*     /\* { .execute = print_execute, .taskid = 40 }, *\/ */
  /*   } */
  /* }, */
#elif defined(SWINGOUT_GROUPED)
  {
    .hostname = "128.208.6.67", .port = 7001,	// swingout1
    .executors = {
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 1, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 2, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 3, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
      { .execute = spout_execute, .init = spout_init, .spout = true, .taskid = 4, .outtasks = { 10, 11, 12, 13 }, .grouper = fields_grouping },
    }
  },
  {
    .hostname = "128.208.6.106", .port = 7002,	// bigfish
    /* .hostname = "128.208.6.129", .port = 7002,	// swingout4 */
    .executors = {
      { .execute = count_execute, .init = count_init, .taskid = 10, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 11, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 12, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
      { .execute = count_execute, .init = count_init, .taskid = 13, .outtasks = { 20, 21, 22, 23 }, .grouper = fields_grouping },
    }
  },
  {
    .hostname = "128.208.6.236", .port = 7003,	// swingout3
    .executors = {
      { .execute = rank_execute, .taskid = 20, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 21, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 22, .outtasks = { 30 }, .grouper = global_grouping },
      { .execute = rank_execute, .taskid = 23, .outtasks = { 30 }, .grouper = global_grouping },
    }
  },
  {
    .hostname = "128.208.6.236", .port = 7004,	// swingout3
    .executors = {
      { .execute = rank_execute, .taskid = 30, .outtasks = { 0 }, .grouper = global_grouping },
      /* { .execute = print_execute, .taskid = 40 }, */
    }
  },
#else
#	error Need to define a topology!
#endif
  { .hostname = NULL }
};


#if defined(BIGFISH) || defined(BIGFISH_FLEXNIC)
#	define PROC_FREQ	1600000.0	// bigfish
#else
#	define PROC_FREQ	2200000.0	// swingout
#endif

#define PROC_FREQ_MHZ	(uint64_t)(PROC_FREQ / 1000)
