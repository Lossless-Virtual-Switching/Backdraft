#ifndef float_LL
#define float_LL
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

struct node {
	float val;
	struct node * next;
};

struct floatList {
	struct node *head;
	struct node *tail;
	long long int count_node;
	struct node *pool;
};

typedef struct floatList fList;

/*
 * Append a new value to the linkedlist
 * */
int append_flist(fList *list, float val);

/*
 * Sort the linkedlist inplace.
 * Insertion sort.
 * */
int sort_flist(fList *list);

/*
 * Insert an element to the list in a way that 
 * asc. sort order is kept.
 * */
int insert_flist_maintain_sort(fList *list, float val);

/*
 * Print fList to stdout
 * */
void print_flist(fList *list);

/*
 * Get value at the given index
 * */
float get_flist(fList *list, long long int index);

/*
 * Remove an element
 * */
float remove_flist(fList *list, long long int index);

/*
 * Allocate a new fList and initialize it
 * */
fList *new_flist(int pool_size);

/*
 * Free allocated memory
 * */
int free_flist(fList *list);

#endif
