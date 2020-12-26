/*
 * I seperated list_head definition form behaviour implementation.
 * The reason is this module is included in c++ code and implementation
 * does not need to be carried their.
 *
 * - Farbod Shahinfar
 * */
/**
 *
 * I grub it from linux kernel source code and fix it for user space
 * program. Of course, this is a GPL licensed header file.
 *
 * Here is a recipe to cook list.h for user space program
 *
 * 1. copy list.h from linux/include/list.h
 * 2. remove
 *     - #ifdef __KERNE__ and its #endif
 *     - all #include line
 *     - prefetch() and rcu related functions
 * 3. add macro offsetof() and container_of
 *
 * - kazutomo@mcs.anl.gov
 */
#ifndef _LINUX_LIST_HEAD_H
#define _LINUX_LIST_HEAD_H


/**
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */
struct list_head {
	struct list_head *next, *prev;
};


/*
 * Double linked lists with a single pointer list head.
 * Mostly useful for hash tables where the two pointer list head is
 * too wasteful.
 * You lose the ability to access the tail in O(1).
 */

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

#endif
