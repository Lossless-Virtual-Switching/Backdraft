/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef IOKVS_H_
#define IOKVS_H_

#define GNU_SOURCE_
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>


/******************************************************************************/
/* Settings */

/** Configurable settings */
struct settings {
    /** Size of log segments in bytes */
    size_t segsize;
    /** Maximal number of segments to use  */
    size_t segmaxnum;
    /** Size of seqment clean queue */
    size_t segcqsize;
    /** Segment cleaning ratio */
    double clean_ratio;
    /** UDP port to listen on */
    uint16_t udpport;
    /** Verbosity for log messages. */
    uint8_t verbose;
    /** Number of cores */
    uint8_t numcores;
    /** Config file */
    char *config_file;
};

/** Global settings */
extern struct settings settings;

/** Initialize global settings from command-line. */
int settings_init(int argc, char *argv[]);


/******************************************************************************/
/* Hash table operations */

/** Initialize hash table. */
void hasht_init(void);

/** Prefetch hash table slot */
void hasht_prefetch1(uint32_t hv);

/** Prefetch matching items */
void hasht_prefetch2(uint32_t hv);

/**
 * Lookup key in hash table.
 * @param key  Key
 * @param klen Length of key in bytes
 * @param hv   Hash of key
 * @return Pointer to item or NULL
 */
struct item *hasht_get(const void *key, size_t klen, uint32_t hv);

/**
 * Insert item into hash table
 * @param it  Item
 * @param cas If != NULL, will only store `it' if cas is the object currently
 *            stored for the key (compare and set).
 */
void hasht_put(struct item *it, struct item *cas);


/******************************************************************************/
/* Item Allocation */
struct segment_header;
struct item;
/**
 * Item allocator struct. Should be considered to be opaque outside ialloc.c
 *
 * This struct is slightly ugly, as it is split up into 3 parts to reduce false
 * sharing as much as possible.
 */
struct item_allocator {
    /***********************************************************/
    /* Part 1: mostly read-only for maintenance and worker */

    /* Reserved segment for log cleaning in case we run out */
    struct segment_header *reserved;
    /* Queue for communication  */
    struct item **cleanup_queue;

    uint8_t pad_0[48];
    /***********************************************************/
    /* Part 2: Only accessed by worker threads */

    /* Current segment */
    struct segment_header *cur;
    /* Head pointer in cleanup queue */
    size_t cq_head;
    /* Clenanup counter, limits mandatory cleanup per request */
    size_t cleanup_count;

    uint8_t pad_1[40];
    /***********************************************************/
    /* Part 3: Only accessed by maintenance threads */

    /* Oldest segment */
    struct segment_header *oldest;
    /* Tail pointer for cleanup queue */
    size_t cq_tail;
    /*  */
    struct segment_header *cleaning;
    /*  */
    size_t clean_offset;
};

_Static_assert(offsetof(struct item_allocator, cur) % 64 == 0,
        "Alignment in struct item_allocator broken 1");
_Static_assert(offsetof(struct item_allocator, oldest) % 64 == 0,
        "Alignment in struct item_allocator broken 2");

/** Initialize item allocation. Prepares memory regions etc. */
void ialloc_init(void);

/** Initialize an item allocator instance. */
void ialloc_init_allocator(struct item_allocator *ia);

/**
 * Allocate an item.
 *
 * Note this function has two modes: cleanup and non-cleanup. In cleanup mode,
 * the allocator will use the segment reserved for log cleanup if no other
 * allocation is possible, otherwise it will just return NULL and leave the
 * reserved segment untouched.
 *
 * @param ia      Allocator instance
 * @param total   Total number of bytes (includes item struct)
 * @param cleanup true if this allocation is for a cleanup operation
 * @return Allocated item or NULL.
 */
struct item *ialloc_alloc(struct item_allocator *ia, size_t total,
        bool cleanup);

/**
 * Free an item.
 * @param it    Item
 * @param total Total number of bytes (includes item struct)
 */
void ialloc_free(struct item *it, size_t total);

/**
 * Get item from cleanup queue for this allocator.
 * @param ia   Allocator instance
 * @param idle true if there are currently no pending requests, false otherwise
 * @return Item or NULL
 */
struct item *ialloc_cleanup_item(struct item_allocator *ia, bool idle);

/**
 * Resets per-request cleanup counters. Should be called when a new request is
 * ready to be processed before calling ialloc_cleanup_item.
 */
void ialloc_cleanup_nextrequest(struct item_allocator *ia);

/**
 * Dispatch log cleanup operations for this instance, if required. To be called
 * from maintenance thread.
 */
void ialloc_maintenance(struct item_allocator *ia);


/******************************************************************************/
/* Items */

/**
 * Item.
 * The item struct is immediately followed by first the key, and then the
 * associated value.
 */
struct item {
    /** Next item in the hash chain. */
    struct item *next;
    /** Hash value for this item */
    uint32_t hv;
    /** Length of value in bytes */
    uint32_t vallen;
    /** Reference count */
    volatile uint16_t refcount;
    /** Length of key in bytes */
    uint16_t keylen;
    /** Flags (currently unused, but provides padding) */
    uint32_t flags;
};

/** Get pointer to the item's key */
static inline void *item_key(struct item *it)
{
    return it + 1;
}

/** Get pointer to the item's value */
static inline void *item_value(struct item *it)
{
    return (void *) ((uintptr_t) (it + 1) + it->keylen);
}

/** Total number of bytes for this item (includes item struct) */
static inline size_t item_totalsz(struct item *it)
{
    return sizeof(*it) + it->vallen + it->keylen;
}

/** Increment item's refcount (original refcount must not be 0). */
static inline void item_ref(struct item *it)
{
    uint16_t old;
    old = __sync_add_and_fetch(&it->refcount, 1);
    assert(old != 1);
}

/**
 * Increment item's refcount if it is not zero.
 * @return true if the refcount was increased, false otherwise.
 */
static inline bool item_tryref(struct item *it)
{
    uint16_t c;
    do {
        c = it->refcount;
        if (c == 0) {
            return false;
        }
    } while (!__sync_bool_compare_and_swap(&it->refcount, c, c + 1));
    return true;
}

/**
 * Decrement item's refcount, and free item if refcount = 0.
 * The original refcount must be > 0.
 */
static inline void item_unref(struct item *it)
{
    uint16_t c;
    assert(it->refcount > 0);
    if ((c = __sync_sub_and_fetch(&it->refcount, 1)) == 0) {
        ialloc_free(it, item_totalsz(it));
    }
}

/** Wrapper for transport code */
static inline void myt_item_release(void *it)
{
    item_unref(it);
}




uint32_t jenkins_hash(const void *key, size_t length);

#endif // ndef IOKVS_H_
