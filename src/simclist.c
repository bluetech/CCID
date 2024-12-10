/*
 * Copyright (c) 2007,2008,2009,2010,2011 Mij <mij@bitchx.it>
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

/* SimCList implementation, version 1.6 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <assert.h>

/*
 * how many elems to keep as spare. During a deletion, an element
 * can be saved in a "free-list", not free()d immediately. When
 * latter insertions are performed, spare elems can be used instead
 * of malloc()ing new elems.
 *
 * about this param, some values for appending
 * 10 million elems into an empty list:
 * (#, time[sec], gain[%], gain/no[%])
 * 0    2,164   0,00    0,00    <-- feature disabled
 * 1    1,815   34,9    34,9
 * 2    1,446   71,8    35,9    <-- MAX gain/no
 * 3    1,347   81,7    27,23
 * 5    1,213   95,1    19,02
 * 8    1,064   110,0   13,75
 * 10   1,015   114,9   11,49   <-- MAX gain w/ likely sol
 * 15   1,019   114,5   7,63
 * 25   0,985   117,9   4,72
 * 50   1,088   107,6   2,15
 * 75   1,016   114,8   1,53
 * 100  0,988   117,6   1,18
 * 150  1,022   114,2   0,76
 * 200  0,939   122,5   0,61    <-- MIN time
 */
#ifndef SIMCLIST_MAX_SPARE_ELEMS
#define SIMCLIST_MAX_SPARE_ELEMS        5
#endif

#include "simclist.h"

static inline struct list_entry_s *list_findpos(const list_t *restrict l, int posstart);

/* list initialization */
int list_init(list_t *restrict l) {
    if (l == NULL) return -1;

    memset(l, 0, sizeof *l);

    l->numels = 0;

    /* head/tail sentinels and mid pointer */
    l->head_sentinel = (struct list_entry_s *)malloc(sizeof(struct list_entry_s));
    l->tail_sentinel = (struct list_entry_s *)malloc(sizeof(struct list_entry_s));
    if (NULL == l->tail_sentinel || NULL == l->head_sentinel)
        return -1;

    l->head_sentinel->next = l->tail_sentinel;
    l->tail_sentinel->prev = l->head_sentinel;
    l->head_sentinel->prev = l->tail_sentinel->next = l->mid = NULL;
    l->head_sentinel->data = l->tail_sentinel->data = NULL;

    /* free-list attributes */
    l->spareels = (struct list_entry_s **)malloc(SIMCLIST_MAX_SPARE_ELEMS * sizeof(struct list_entry_s *));
    l->spareelsnum = 0;
    if (NULL == l->spareels)
        return -1;

    return 0;
}

void list_destroy(list_t *restrict l) {
    unsigned int i;

    list_clear(l);
    for (i = 0; i < l->spareelsnum; i++) {
        free(l->spareels[i]);
    }
    free(l->spareels);
    free(l->head_sentinel);
    free(l->tail_sentinel);
}

int list_append(list_t *restrict l, const void *data) {
    return list_insert_at(l, data, l->numels);
}

void *list_get_at(const list_t *restrict l, unsigned int pos) {
    struct list_entry_s *tmp;

    tmp = list_findpos(l, pos);

    return (tmp != NULL ? tmp->data : NULL);
}

/* set tmp to point to element at index posstart in l */
static inline struct list_entry_s *list_findpos(const list_t *restrict l, int posstart) {
    struct list_entry_s *ptr;
    float x;
    int i;

    if (NULL == l->head_sentinel || NULL == l->tail_sentinel)
		return NULL;

    /* accept 1 slot overflow for fetching head and tail sentinels */
    if (posstart < -1 || posstart > (int)l->numels) return NULL;

    if( l->numels != 0 )
        x = (float)(posstart+1) / l->numels;
    else
        x = 1;
    if (x <= 0.25) {
        /* first quarter: get to posstart from head */
        for (i = -1, ptr = l->head_sentinel; i < posstart; ptr = ptr->next, i++);
    } else if (x < 0.5) {
        /* second quarter: get to posstart from mid */
        for (i = (l->numels-1)/2, ptr = l->mid; i > posstart; ptr = ptr->prev, i--);
    } else if (x <= 0.75) {
        /* third quarter: get to posstart from mid */
        for (i = (l->numels-1)/2, ptr = l->mid; i < posstart; ptr = ptr->next, i++);
    } else {
        /* fourth quarter: get to posstart from tail */
        for (i = l->numels, ptr = l->tail_sentinel; i > posstart; ptr = ptr->prev, i--);
    }

    return ptr;
}

int list_insert_at(list_t *restrict l, const void *data, unsigned int pos) {
    struct list_entry_s *lent, *succ, *prec;

    if (pos > l->numels) return -1;

    /* this code optimizes malloc() with a free-list */
    if (l->spareelsnum > 0) {
        lent = l->spareels[l->spareelsnum-1];
        l->spareelsnum--;
    } else {
        lent = (struct list_entry_s *)malloc(sizeof(struct list_entry_s));
        if (lent == NULL)
            return -1;
    }

    lent->data = (void*)data;

    /* actually append element */
    prec = list_findpos(l, pos-1);
    if (NULL == prec)
    {
        free(lent->data);
        free(lent);
        return -1;
    }
    succ = prec->next;

    prec->next = lent;
    lent->prev = prec;
    lent->next = succ;
    succ->prev = lent;

    l->numels++;

    /* fix mid pointer */
    if (l->numels == 1) { /* first element, set pointer */
        l->mid = lent;
    } else if (l->numels % 2) {    /* now odd */
        if (pos >= (l->numels-1)/2) l->mid = l->mid->next;
    } else {                /* now even */
        if (pos <= (l->numels-1)/2) l->mid = l->mid->prev;
    }

    return 1;
}

int list_clear(list_t *restrict l) {
    struct list_entry_s *s;
    unsigned int numels;

    /* will be returned */
    numels = l->numels;

    if (l->head_sentinel && l->tail_sentinel) {
        /* spare a loop conditional with two loops: sparing elems and freeing elems */
        for (s = l->head_sentinel->next; l->spareelsnum < SIMCLIST_MAX_SPARE_ELEMS && s != l->tail_sentinel; s = s->next) {
            /* move elements as spares as long as there is room */
            l->spareels[l->spareelsnum++] = s;
        }
        while (s != l->tail_sentinel) {
            /* free the remaining elems */
            s = s->next;
            free(s->prev);
        }
        l->head_sentinel->next = l->tail_sentinel;
        l->tail_sentinel->prev = l->head_sentinel;
    }
    l->numels = 0;
    l->mid = NULL;

    return numels;
}

unsigned int list_size(const list_t *restrict l) {
    return l->numels;
}
