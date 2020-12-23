#ifndef _SET_H
#define _SET_H

// ============================================================
// Implementation of a linked list
// ============================================================
typedef struct list_node {
  void *value;
  struct list_node *next;
} list_node_t;

typedef struct list {
  size_t size;
  list_node_t *head;
  list_node_t *tail;
} list_t;

list_t *new_list(void);
int append_list(list_t *list, void *value);
int contains_list(list_t *list, void *value);
int remove_list(list_t *list, void *value);
void *element_list(list_t *list, size_t index);

// ============================================================
// Implementation of a hash set
// ============================================================
typedef struct hash_set {
  int num_buckets;
  int size;
  list_t *buckets;
} hash_set_t;

hash_set_t *new_hash_set(size_t num_buckets);
int add_hash_set(hash_set_t *set, void *value, size_t elem_size);
int remove_hash_set(hash_set_t *set, void *value, size_t elem_size);
int contains_hash_set(hash_set_t *set, void *value, size_t elem_size);
#endif
