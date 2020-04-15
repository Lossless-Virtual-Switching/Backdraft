#include <stdlib.h>
#include "percentile.h"

static int log_b(float v, float b)
{
	int res = 0;
	if (v < 0)
		return -1;

	while(v > 1) {
		res++;
		v = v / b;
	}
	return res;
}

static float pw(float b, int p)
{
	int i;
	float res = 1;
	if (p < 0)
		return -1;
	for (i = 0; i< p; i++)
		res = res * b;
	return res;
}


struct p_hist *new_p_hist(int count_bucket)
{
	struct p_hist *ptr = malloc(sizeof(struct p_hist));
	ptr->count_bucket = count_bucket;
	ptr->count_values = 0;
	ptr->buckets = malloc(count_bucket * sizeof(int));
	return ptr;
}

void free_p_hist(struct p_hist *hist)
{
	free(hist->buckets);
	free(hist);
}

void add_number_to_p_hist(struct p_hist *hist, float value)
{
	int index = log_b(value, r2);
	if (index >= hist->count_bucket)
		index = hist->count_bucket - 1;
	hist->buckets[index]++;
	hist->count_values++;
}


float get_percentile(struct p_hist *hist, float percentile)
{
	int i;
	float tmp = 0;
	float p = 0;
	float x0, x1, y0 = 0, y1 = 0;
	float y = percentile; // (0, 1)
	for (i = 0; i < hist->count_bucket; i++) {
		tmp += hist->buckets[i];
		y0 = y1;
		y1 = tmp / (float)hist->count_values;
		if (y1 >= y) {
			break;
		}
	}
	if (y1 == y) {
		p = pw(r2, i);
	} else {
		x0 = pw (r2, i-1);
		x1 = pw(r2, i);
		p = x0 + (x1 - x0)*((y - y0)/(y1 - y0));
	}
	return p;
}
