/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"

#define MIN(a, b) ((a < b) ? a : b)
#define GOLDEN_RATIO_32 0x61C88647
#define HASH(val) \
    (((val) * (GOLDEN_RATIO_32)) >> (32 - (cache_size_bits))) & (cache_size - 1)

static uint32_t cache_size, cache_size_bits;
struct list_head {
    struct list_head *prev, *next;
};

struct hlist_head {
    struct hlist_node *first;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};

typedef struct {
    void *value;
    uint32_t key;
    uint32_t frequency;
    struct list_head list;
    struct hlist_node ht_list;
} lfu_entry_t;

typedef struct {
    struct hlist_head *ht_list_head;
} hashtable_t;

typedef struct cache {
    struct list_head *lists[1000];
    struct list_head replace_list;
    uint32_t list_size;
    hashtable_t *map;
    uint32_t capacity;
} cache_t;

static inline void INIT_LIST_HEAD(struct list_head *head)
{
    head->next = head;
    head->prev = head;
}

static inline int list_empty(const struct list_head *head)
{
    return (head->next == head);
}

static inline void list_add(struct list_head *node, struct list_head *head)
{
    struct list_head *next = head->next;

    next->prev = node;
    node->next = next;
    node->prev = head;
    head->next = node;
}

static inline void list_del(struct list_head *node)
{
    struct list_head *next = node->next;
    struct list_head *prev = node->prev;

    next->prev = prev;
    prev->next = next;
}

static inline void list_del_init(struct list_head *node)
{
    list_del(node);
    INIT_LIST_HEAD(node);
}

#define list_entry(node, type, member) container_of(node, type, member)

#define list_last_entry(head, type, member) \
    list_entry((head)->prev, type, member)

#ifdef __HAVE_TYPEOF
#define list_for_each_entry_safe(entry, safe, head, member)                \
    for (entry = list_entry((head)->next, __typeof__(*entry), member),     \
        safe = list_entry(entry->member.next, __typeof__(*entry), member); \
         &entry->member != (head); entry = safe,                           \
        safe = list_entry(safe->member.next, __typeof__(*entry), member))
#else
#define list_for_each_entry_safe(entry, safe, head, member, type) \
    for (entry = list_entry((head)->next, type, member),          \
        safe = list_entry(entry->member.next, type, member);      \
         &entry->member != (head);                                \
         entry = safe, safe = list_entry(safe->member.next, type, member))
#endif

#define INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL)

static inline void INIT_HLIST_NODE(struct hlist_node *h)
{
    h->next = NULL;
    h->pprev = NULL;
}

static inline int hlist_empty(const struct hlist_head *h)
{
    return !h->first;
}

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
    struct hlist_node *first = h->first;
    n->next = first;
    if (first)
        first->pprev = &n->next;

    h->first = n;
    n->pprev = &h->first;
}

static inline bool hlist_unhashed(const struct hlist_node *h)
{
    return !h->pprev;
}

static inline void hlist_del(struct hlist_node *n)
{
    struct hlist_node *next = n->next;
    struct hlist_node **pprev = n->pprev;

    *pprev = next;
    if (next)
        next->pprev = pprev;
}

static inline void hlist_del_init(struct hlist_node *n)
{
    if (hlist_unhashed(n))
        return;
    hlist_del(n);
    INIT_HLIST_NODE(n);
}

#define hlist_entry(ptr, type, member) container_of(ptr, type, member)

#ifdef __HAVE_TYPEOF
#define hlist_entry_safe(ptr, type, member)                  \
    ({                                                       \
        typeof(ptr) ____ptr = (ptr);                         \
        ____ptr ? hlist_entry(____ptr, type, member) : NULL; \
    })
#else
#define hlist_entry_safe(ptr, type, member) \
    (ptr) ? hlist_entry(ptr, type, member) : NULL
#endif

#ifdef __HAVE_TYPEOF
#define hlist_for_each_entry(pos, head, member)                              \
    for (pos = hlist_entry_safe((head)->first, typeof(*(pos)), member); pos; \
         pos = hlist_entry_safe((pos)->member.next, typeof(*(pos)), member))
#else
#define hlist_for_each_entry(pos, head, member, type)              \
    for (pos = hlist_entry_safe((head)->first, type, member); pos; \
         pos = hlist_entry_safe((pos)->member.next, type, member))
#endif

cache_t *cache_create(int size_bits)
{
    cache_t *cache = malloc(sizeof(cache_t));
    if (!cache)
        return NULL;
    cache_size_bits = size_bits;
    cache_size = 1 << size_bits;

    for (int i = 0; i < 1000; i++) {
        cache->lists[i] = malloc(sizeof(struct list_head));
        INIT_LIST_HEAD(cache->lists[i]);
    }

    cache->map = malloc(sizeof(hashtable_t));
    if (!cache->map) {
        free(cache->lists);
        free(cache);
        return NULL;
    }
    cache->map->ht_list_head = malloc(cache_size * sizeof(struct hlist_head));
    if (!cache->map->ht_list_head) {
        free(cache->map);
        free(cache->lists);
        free(cache);
        return NULL;
    }
    for (uint32_t i = 0; i < cache_size; i++) {
        INIT_HLIST_HEAD(&cache->map->ht_list_head[i]);
    }
    cache->list_size = 0;
    cache->capacity = cache_size;
    return cache;
}

void *cache_get(cache_t *cache, uint32_t key)
{
    if (!cache->capacity || hlist_empty(&cache->map->ht_list_head[HASH(key)]))
        return NULL;

    lfu_entry_t *entry = NULL;
#ifdef __HAVE_TYPEOF
    hlist_for_each_entry (entry, &cache->map->ht_list_head[HASH(key)], ht_list)
#else
    hlist_for_each_entry (entry, &cache->map->ht_list_head[HASH(key)], ht_list,
                          lfu_entry_t)
#endif
    {
        if (entry->key == key)
            break;
    }
    if (!entry || entry->key != key)
        return NULL;

    /* We would translate the block with a frequency of more than 1000 */
    if (entry->frequency < 1000) {
        list_del_init(&entry->list);
        list_add(&entry->list, cache->lists[entry->frequency++]);
    }

    /* return NULL if cache miss */
    return entry->value;
}

void *cache_put(cache_t *cache, uint32_t key, void *value)
{
    void *delete_value = NULL;
    assert(cache->list_size <= cache->capacity);
    /* Before adding new element to cach, we should check the status
     * of cache.
     */
    if (cache->list_size == cache->capacity) {
        for (int i = 0; i < 1000; i++) {
            if (!list_empty(cache->lists[i])) {
                lfu_entry_t *delete_target =
                    list_last_entry(cache->lists[i], lfu_entry_t, list);
                list_del_init(&delete_target->list);
                hlist_del_init(&delete_target->ht_list);
                delete_value = delete_target->value;
                cache->list_size--;
                free(delete_target);
                break;
            }
        }
    }
    lfu_entry_t *new_entry = malloc(sizeof(lfu_entry_t));
    new_entry->key = key;
    new_entry->value = value;
    new_entry->frequency = 0;
    list_add(&new_entry->list, cache->lists[new_entry->frequency++]);
    cache->list_size++;
    hlist_add_head(&new_entry->ht_list, &cache->map->ht_list_head[HASH(key)]);
    assert(cache->list_size <= cache->capacity);
    return delete_value;
}

void cache_free(cache_t *cache, void (*callback)(void *))
{
    for (int i = 0; i < 1000; i++) {
        if (list_empty(cache->lists[i]))
            continue;
        lfu_entry_t *entry, *safe;
#ifdef __HAVE_TYPEOF
        list_for_each_entry_safe (entry, safe, cache->lists[i], list)
#else
        list_for_each_entry_safe (entry, safe, cache->lists[i], list,
                                  arc_entry_t)
#endif
            callback(entry->value);
    }
    free(cache->map->ht_list_head);
    free(cache->map);
    free(cache);
}