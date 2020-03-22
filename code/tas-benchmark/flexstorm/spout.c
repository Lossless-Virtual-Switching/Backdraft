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
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdbool.h>

#include "storm.h"

#define TWITTER_FEED

// in seconds
#ifndef LOCAL
#	define WAIT_TIME	2
#else
#	define WAIT_TIME	1
#endif

#ifndef TWITTER_FEED
/* static char *words[] = {"nathan", "mike", "jackson", "golda", "bertels"}; */
static char *words[] = {"nathan", "jackson", "golda", "bertels"};
static size_t numwords = sizeof(words) / sizeof(words[0]);
#endif

struct spout_state {
  char		**words;
  size_t	numwords;
};

void spout_execute(const struct tuple *t, struct executor *self)
{
  struct spout_state *st = self->state;
  struct tuple myt;
  static __thread bool send = false;
#ifndef TWITTER_FEED
  static __thread bool init = false;
  static __thread struct random_data mybuf;
  static __thread char statebuf[256];
  int r, i;
#else
  static __thread int i = 0;
#endif

#ifndef TWITTER_FEED
  if(!init) {
    r = initstate_r(self->taskid, statebuf, 256, &mybuf);
    assert(r == 0);
    init = true;
  }

  r = random_r(&mybuf, &i);
  assert(r == 0);
  i %= st->numwords;
#endif

  assert(t == NULL);

  if(!send) {
    usleep(WAIT_TIME * 1000000);
    send = true;
  }

  /* usleep(100000); */
  /* sleep(1); */

  memset(&myt, 0, sizeof(struct tuple));
  strcpy(myt.v[0].str, st->words[i]);
  /* printf("%d: Spout emitting '%s'.\n", self->taskid, st->words[i]); */
  tuple_send(&myt, self);

#ifdef TWITTER_FEED
  i = (i + 1) % st->numwords;
  /* if(i == 0) { */
  /*   printf("%d: Wrapped around!\n", self->taskid); */
  /* } */
#endif
}

void spout_init(struct executor *self)
{
  assert(self != NULL);
  self->state = malloc(sizeof(struct spout_state));
  assert(self->state != NULL);
  struct spout_state *st = self->state;

#ifndef TWITTER_FEED
  st->words = words;
  st->numwords = numwords;
#else
    // Load twitter feed
    char filename[256];
    snprintf(filename, 256, "unames-%d.txt", self->taskid);
    FILE *f = fopen(filename, "r");
    assert(f != NULL);

    int r = fscanf(f, "%zu\n", &st->numwords);
    assert(r == 1);

    printf("Reading %zu names\n", st->numwords);

    st->words = malloc(st->numwords * sizeof(char *));
    assert(st->words != NULL);

    /* int lastpct = 0; */
    for(size_t i = 0; i < st->numwords; i++) {
      /* int newpct = (i * 100) / st->numwords; */
      /* if(newpct > lastpct + 25) { */
      /* 	printf("%d: Done %u %%\n", self->taskid, newpct); */
      /* 	lastpct = newpct; */
      /* } */

      st->words[i] = malloc(MAX_STR);
      assert(st->words[i] != NULL);
      char *ret = fgets(st->words[i], MAX_STR, f);
      assert(ret != NULL);
      assert(st->words[i][strlen(st->words[i]) - 1] == '\n');
      st->words[i][strlen(st->words[i]) - 1] = '\0';
    }

    fclose(f);

    printf("%d: Done reading.\n", self->taskid);
#endif
}
