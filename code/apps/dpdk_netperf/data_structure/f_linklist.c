#include "f_linklist.h"
#include <assert.h>


/*
 * Append a new value to the linkedlist
 * */
int append_flist(fList *list, float val)
{
	struct node *new_node;
	if (list->pool == 0) {
		new_node = malloc(sizeof(struct node));
		assert (new_node != 0);
	} else {
		// use pool for faster allocation
		new_node = list->pool;
		list->pool = new_node->next;
		new_node->next = 0;
	}
	new_node->val = val;

	if (list->count_node == 0) {
		// first node
		list->head = new_node;
		list->tail = new_node;
		list->count_node++;
	} else if (list->count_node > 0) {
		list->tail->next = new_node;
		list->tail = new_node;
		list->count_node++;
	} else {
		printf("list size is negative!");
		return 1;
	}
	return 0;
}

/*
 * Insert an element to the list in a way that 
 * asc. sort order is kept.
 * */
int insert_flist_maintain_sort(fList *list, float val)
{
	struct node *ptr, *prev_ptr;
	struct node *new_node = malloc(sizeof(struct node));
	assert  (new_node != 0);

	new_node->val = val;
	if (list->count_node == 0) {
		// first node 
		list->head = new_node;
		list->tail = new_node;
		list->count_node++;
		return 0;
	}

	ptr = list->head;
	prev_ptr = 0;
	while (ptr != 0 && ptr->val < val) {
		prev_ptr= ptr;
		ptr = ptr->next;
	}
	if (prev_ptr == 0) {
		// place at head
		list->head = new_node;
		new_node->next = ptr;
	} else if (ptr == 0) {
		// place at tail
		prev_ptr->next = new_node;
		new_node->next = ptr;
		list->tail = new_node;

	} else {
		// place in middle
		prev_ptr->next = new_node;
		new_node->next = ptr;
	}
	list->count_node++;
	return 0;

}

/*
 * Print fList to stdout
 * */
void print_flist(fList *list)
{
	struct node *ptr;
	ptr = list->head;
	printf("[");
	while (ptr != 0) {
		printf("%f, ", ptr->val);
		ptr = ptr->next;
	}
	printf("]\n");
}

/*
 * Get value at the given index
 * */
float get_flist(fList *list, long long int index)
{
	long long int i;
	struct node *ptr;
	if (index >= list->count_node) {
		return -1;
	}

	ptr = list->head;
	for (i = 0; i < index; i++)
		ptr = ptr->next;
	return ptr->val;
}

float remove_flist(fList *list, long long int index)
{
	int i;
	float val;
	struct node *ptr, *prev;
	if (index >= list->count_node) {
		return -1;
	}

	ptr = list->head;
	prev = ptr;
	for (i = 0; i < index - 1; i++)
		prev = prev->next;
	ptr = prev->next;

	prev->next = ptr->next;
	list->count_node--;

	if (index == 0) {
		list->head = prev;
	}
	if (ptr->next == NULL) {
		list->tail = prev;
	}
	

	val = ptr->val;

	// add the element to the pool
	prev = list->pool;
	list->pool = ptr;
	ptr->next =prev;

	return val;
}

__attribute__((unused))
static int _insertion_sort_flist(fList *list)
{
	struct node *ptr, *prev;
	struct node *sptr, *sprev;
	float val;

	if (list->count_node == 0) return 0;

	ptr = list->head;
	prev = ptr;
	while (ptr != 0) {
		if (ptr->val >= prev->val) {
			prev = ptr;
			ptr = ptr->next;
		} else {
			prev->next = ptr->next;
			val  =ptr->val;
			sptr = list->head;
			sprev = 0;
			while (sptr != 0 && sptr->val < val) {
				sprev = sptr;
				sptr = sptr->next;
			}
			if (sprev == 0) {
				list->head = ptr;
				ptr->next = sptr;
			} else if (sptr == 0) {
				list->tail = ptr;
				sprev->next = ptr;
				ptr->next = sptr;
			} else {
				sprev->next = ptr;
				ptr->next = sptr;
			}
			ptr = prev;
		}
	}
	return 0;
}

/*
 * Partition func. of qsort algorithm.
 * returns the pivot node
 * */
static struct node *_qsort_partition(struct node *begin, struct node *end, struct node **newHead, struct node **newEnd)
{
	struct node *pivot, *ptr, *prev;
	float pivot_val;

	*newHead = 0;
	*newEnd = end;

	pivot = end;
	pivot_val = pivot->val;
	ptr = begin;
	prev = begin;

	while (ptr != pivot) {
		if (ptr->val <= pivot_val) {
			if (*newHead == 0) *newHead = ptr;
			prev = ptr;
			ptr = ptr->next;
		} else {
			prev->next = ptr->next;
			ptr->next = 0; 
			(*newEnd)->next = ptr;
			*newEnd = ptr;
			ptr = prev;
		}
	}
	if (*newHead == 0) *newHead = pivot;
	return pivot;
}

static int _qsort_rec(struct node *begin, struct node *end, struct node **sortedHead, struct node **sortedTail) {
	struct node *newHead, *newEnd, *pivot;
	struct node *ptrHEnd, *tmp;

	pivot = _qsort_partition(begin, end, &newHead, &newEnd);

	if (newHead != pivot) {
		ptrHEnd = newHead;
		while (ptrHEnd->next != pivot)
			ptrHEnd = ptrHEnd->next;
		_qsort_rec(newHead, ptrHEnd, sortedHead, &tmp);
		tmp->next = pivot;
	}
	_qsort_rec(pivot->next, newEnd, &tmp, sortedTail);
	return 0;
}

static int _qsort_flist(fList *list)
{
	struct node *head, *tail;

	if (list->count_node < 2)
		return 0;

	_qsort_rec(list->head, list->tail, &head, &tail);
	list->head = head;
	list->tail = tail;	
	return 0;
}

/*
 * Sort the linkedlist inplace.
 * */
int sort_flist(fList *list)
{
	return _qsort_flist(list);
}

/*
 * Allocate a new fList and initialize it
 * */
fList *new_flist(int pool_size)
{
	int i;
	struct node *pool, *pool_ptr;
	fList *ptr = malloc(sizeof(fList));
	ptr->head = 0;
	ptr->tail = 0;
	ptr->count_node = 0;
	// generate pool
	pool = malloc(sizeof(struct node));
	assert(pool != 0);
	pool_ptr = pool;
	for (i = 0; i < pool_size - 1; i++) {
		pool_ptr->next = malloc(sizeof(struct node));
		assert(pool_ptr->next != 0);
		pool_ptr = pool_ptr->next;
	}
	pool_ptr->next = 0;  // end of pool
	ptr->pool = pool;
	return ptr;
}

/*
 * Free allocated memory
 * */
int free_flist(fList *list)
{
	struct node *ptr, *prev;
	if (list->count_node == 0) return 0;

	ptr = list->head;
	prev = 0;
	while (ptr != 0) {
		prev = ptr;
		ptr = ptr->next;
		free(prev);
	}
	ptr = list->pool;
	while (ptr !=0){
		prev = ptr;
		ptr = ptr->next;
		free(prev);
	}
	free(list);
	return 0;
}
