/*
 * Copyright (c) 2007,2008 Mij <mij@bitchx.it>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


/*
 * SimCList library. See http://mij.oltrelinux.com/devel/simclist
 */


#ifndef SIMCLIST_H
#define SIMCLIST_H

#include <inttypes.h>

/**
 * Type representing list hashes.
 *
 * This is a signed integer value.
 */
typedef int32_t list_hash_t;

/**
 * a comparator of elements.
 *
 * A comparator of elements is a function that:
 *      -# receives two references to elements a and b
 *      -# returns {<0, 0, >0} if (a > b), (a == b), (a < b) respectively
 *
 * It is responsibility of the function to handle possible NULL values.
 */
typedef int (*element_comparator)(const void *a, const void *b);

/* [private-use] list entry -- olds actual user datum */
struct list_entry_s {
    void *data;

    /* doubly-linked list service references */
    struct list_entry_s *next;
    struct list_entry_s *prev;
};

/** list object */
typedef struct {
    struct list_entry_s *head_sentinel;
    struct list_entry_s *tail_sentinel;
    struct list_entry_s *mid;

    unsigned int numels;

    /* array of spare elements */
    struct list_entry_s **spareels;
    unsigned int spareelsnum;
} list_t;

/**
 * initialize a list object for use.
 *
 * @param l     must point to a user-provided memory location
 * @return      0 for success. -1 for failure
 */
int list_init(list_t *restrict l);

/**
 * completely remove the list from memory.
 *
 * This function is the inverse of list_init(). It is meant to be called when
 * the list is no longer going to be used. Elements and possible memory taken
 * for internal use are freed.
 *
 * @param l     list to destroy
 */
void list_destroy(list_t *restrict l);

/**
 * append data at the end of the list.
 *
 * This function is useful for adding elements with a FIFO/queue policy.
 *
 * @param l     list to operate
 * @param data  pointer to user data to append
 *
 * @return      1 for success. < 0 for failure
 */
int list_append(list_t *restrict l, const void *data);

/**
 * retrieve an element at a given position.
 *
 * @param l     list to operate
 * @param pos   [0,size-1] position index of the element wanted
 * @return      reference to user datum, or NULL on errors
 */
void *list_get_at(const list_t *restrict l, unsigned int pos);

/**
 * insert an element at a given position.
 *
 * @param l     list to operate
 * @param data  reference to data to be inserted
 * @param pos   [0,size-1] position index to insert the element at
 * @return      positive value on success. Negative on failure
 */
int list_insert_at(list_t *restrict l, const void *data, unsigned int pos);

/**
 * clear all the elements off of the list.
 *
 * The element datums will not be freed.
 *
 * @see list_delete_range()
 * @see list_size()
 *
 * @param l     list to operate
 * @return      the number of elements removed on success, <0 on error
 */
int list_clear(list_t *restrict l);

/**
 * inspect the number of elements in the list.
 *
 * @param l     list to operate
 * @return      number of elements currently held by the list
 */
unsigned int list_size(const list_t *restrict l);

#endif

