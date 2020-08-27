/*
 * Based on code from this post:
 * https://stackoverflow.com/a/48279287/7510824
 * */

#ifndef __ZIPF_H_
#define __ZIPF_H_

/*
 * Struct for holding the state of zipf generator
 * function.
 * (Instead of using static inside a function I prefered
 * to hold the state in a seperate struct and pass it to
 * the function).
 * This struct holds the harmonic number so it is not needed
 * to be calculated again
 * */
struct zipfgen {
    double *c_probs; // Comulative probability for each rank
    double h;  // Harmonic : (1 / sum (pow(i, s)))
    double s;  // also called alpha
    int n;     // number of ranks
    int (* gen)(struct zipfgen *);  // pointer to generator function
};

/*
 * Create a new zipfgen struct.
 * It will allocate memory and fills the
 * struct.
 * */
struct zipfgen *new_zipfgen(int n, double s);

void free_zipfgen(struct zipfgen *ptr);

/*
 * returns a value in range [1, n]
 * if failed (it should not) it returns -1
 * */
int number_from_zipf_distribution(struct zipfgen *ptr);

#endif
