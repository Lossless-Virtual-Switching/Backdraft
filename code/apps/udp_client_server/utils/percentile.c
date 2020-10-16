#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "percentile.h"

#define r2 1.4142
#define inv_log_r2 (2.885390082)

struct p_hist *new_p_hist(int count_bucket)
{
  struct p_hist *ptr = malloc(sizeof(struct p_hist));
  ptr->count_bucket = count_bucket;
  ptr->count_values = 0;
  ptr->buckets = calloc(count_bucket, sizeof(int));
  return ptr;
}

struct p_hist *new_p_hist_from_max_value(float max_value)
{
  int count_bucket = (log(max_value) * inv_log_r2) + 1;
  return new_p_hist(count_bucket);
}

void free_p_hist(struct p_hist *hist)
{
  free(hist->buckets);
  free(hist);
}

void add_number_to_p_hist(struct p_hist *hist, float value)
{
  int index;
  if (value < r2) {
    index = 0;
  } else {
    index = (log(value) * inv_log_r2);
  }
  if (index >= hist->count_bucket)
    index = hist->count_bucket - 1;
  hist->buckets[index]++;
  hist->count_values++;
}


float get_percentile(struct p_hist *hist, float percentile)
{
  int i; // bucket index
  float tmp = 0; // cumulative (number of items below i-th bucket)
  float p = 0; // results
  float x0, x1, y0 = 0, y1 = 0;
  float y = percentile; // (0, 1)
  for (i = 0; i < hist->count_bucket; i++) {
    y0 = y1;
    tmp += hist->buckets[i];
    y1 = tmp / (float)hist->count_values;
    if (y1 >= y) {
      break;
    }
  }

  // printf("p: %f , i: %d\n", percentile, i);
  if (i == 0) {
    x0 = 0;
    x1 = r2;
  } else {
    x0 = pow(r2, i);
    x1 = pow(r2, i + 1);
  }

  if (x1 > hist->count_values) {
    x1 = hist->count_values;
  }

  // printf("x0: %f, x1: %f\n", x0, x1);
  // printf("y0: %f, y1: %f\n", y0, y1);

  p = x0 + (x1 - x0) * ((y - y0) / (y1 - y0));
  return p;
}

