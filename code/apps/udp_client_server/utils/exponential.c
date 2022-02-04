#include "exponential.h"
#include <math.h>
double get_exponential_sample(double lambda) {
	if (lambda <= 0) return 0;
	double uniform = drand48();
	return -log(uniform) / lambda;
}
