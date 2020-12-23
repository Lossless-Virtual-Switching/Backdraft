/*
 * A simple implementation of Hash Set for Integers
 * */
#include <stdlib.h>
#include "set.h"

// ============================================================
// Implementation of a linked list
// ============================================================
static list_node_t *new_node(void *value, list_node_t *next)
{
  list_node_t *node = malloc(sizeof(list_node_t));
  if (node == NULL)
    return NULL;
  node->value = value;
  node->next = next;
  return node;
}

static void free_node(list_node_t *node)
{
  if (node != NULL)
    free(node);
}

list_t *new_list(void)
{
  list_t *new_list = malloc(sizeof(list_t));
  if (new_list == NULL)
    return NULL;
  new_list->size = 0;
  new_list->head = NULL;
  new_list->tail = NULL;
  return new_list;
}

int append_list(list_t *list, void *value)
{
  list_node_t *node = new_node(value, NULL);
  if (node == NULL)
    return -1;
  if (list->size == 0) {
    list->head = node;
    list->tail = list->head;
  } else {
    list->tail->next = node;
    list->tail = node;
  }
  list->size += 1;
  return 0;
}

static list_node_t *find_node_list(list_t *list, void *value,
                                   list_node_t **prev)
{
  list_node_t *node = list->head;
  list_node_t *before = NULL;
  while(node != NULL && node->value != value) {
    before = node;
    node = node->next;
  }
  if (prev != NULL)
    *prev = before;
  return node;
}

int contains_list(list_t *list, void *value)
{
  list_node_t *node = find_node_list(list, value, NULL);
  if (node == NULL)
    return 0;
  return 1;
}

/* Remove first occurance of `value`
 * */
int remove_list(list_t *list, void *value)
{
  list_node_t *prev;
  list_node_t *node = find_node_list(list, value, &prev);
  if (node == NULL)
    return -1; // not found
  if (prev == NULL) {
    list->head = node->next;
  } else {
    prev->next = node->next;
  }
  if (node == list->tail)
    list->tail = prev;
  list->size--;
  free_node(node);
  return 0;
}

void *element_list(list_t *list, size_t index)
{
  if (index >= list->size)
    return NULL;
  size_t i;
  list_node_t *ptr = list->head;
  for (i = 0; i < index; i++)
    ptr = ptr->next;
  return ptr;
}
// ============================================================
// Implementation of a linked list
// ============================================================

// ============================================================
// Implementation of a hash set
// ============================================================
static size_t _hash(void *elem, size_t elem_size) {
  size_t i;
  size_t h = 0;
  const char *bytes = elem;

  if (elem == NULL) return 0;
  for (i = 0; i < elem_size; i++)
    h = (h + bytes[i]) * 31;
  return h;
}

hash_set_t *new_hash_set(size_t num_buckets)
{
  hash_set_t *new_set = malloc(sizeof(hash_set_t));
  new_set->num_buckets = num_buckets;
  new_set->size = 0;
  new_set->buckets = malloc(num_buckets * sizeof(list_t));
  return new_set;
}

int add_hash_set(hash_set_t *set, void *value, size_t elem_size)
{
  int rc;
  size_t h = _hash(value, elem_size);
  list_t *bucket;
  list_node_t *node;
  h = h % set->num_buckets;
  bucket = &set->buckets[h];
  node = find_node_list(bucket, value, NULL);
  if (node == NULL) {
    rc = append_list(bucket, value);
    if (rc == 0)
      set->size++;
    return rc;
  }
  return 1;
}

int remove_hash_set(hash_set_t *set, void *value, size_t elem_size)
{
  int rc;
  size_t h = _hash(value, elem_size);
  list_t *bucket;
  h = h % set->num_buckets;
  bucket = &set->buckets[h];
  rc = remove_list(bucket, value);
  if (rc == 0)
    set->size--;
  return 0;
}

int contains_hash_set(hash_set_t *set, void *value, size_t elem_size)
{
  size_t h = _hash(value, elem_size);
  list_t *bucket;
  list_node_t *node;
  h = h % set->num_buckets;
  bucket = &set->buckets[h];
  node = find_node_list(bucket, value, NULL);
  if (node == NULL)
    return 0;
  return 1;
}
// ============================================================
// Implementation of a hash set
// ============================================================
