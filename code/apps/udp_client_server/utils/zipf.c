#include <stdlib.h>
#include <math.h>
#include "zipf.h"

struct zipfgen *new_zipfgen(int n, double s)
{
	int i;
	double h = 0;
	struct zipfgen *ptr = malloc(sizeof(struct zipfgen));
	double *c_probs = malloc((n + 1) * sizeof(double));

	for (i = 1; i < n + 1; i++)
		h += 1.0 / pow((double)i, s);

	c_probs[0] = 0;
	for (i = 1; i < n + 1; i++)
		c_probs[i] = c_probs[i - 1] + (1.0 / (pow(i, s) * h));

	ptr->c_probs = c_probs;
	ptr->h = h;
	ptr->s = s;
	ptr->n = n;
	ptr->gen = number_from_zipf_distribution;
	return ptr;
}

void free_zipfgen(struct zipfgen *ptr)
{
	free(ptr->c_probs);
	free(ptr);
}

/*
 * returns a value in range [1, n]
 * if failed (it should not) it returns -1
 * */
int number_from_zipf_distribution(struct zipfgen *ptr)
{
	int low, high, mid;
	double rnd;

	// get a uniform random number
	rnd = ((double)random() / (double)RAND_MAX);

	low = 1;
	high = ptr->n;
	mid = (low + high) / 2;
	while (low <= high) {
		mid = (low + high) / 2;
		if (rnd > ptr->c_probs[mid - 1] && rnd <= ptr->c_probs[mid])
			return mid;

		if (ptr->c_probs[mid] < rnd) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}
	// failed
	return -1;
}

