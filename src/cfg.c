/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "riscv_private.h"
#include "utils.h"

#define SET_SIZE_BITS 10
#define SET_SLOTS_SIZE 32
#define SET_SIZE 1 << SET_SIZE_BITS
#define HASH(val) \
    (((val) * (GOLDEN_RATIO_32)) >> (32 - SET_SIZE_BITS)) & ((SET_SIZE) -1)

typedef struct vector {
    int size;
    struct node *arr[1024];
} vector_t;

typedef struct node {
    block_t *value;
    struct node *left, *right;
    vector_t dom;
} node_t;

/*
 * The set consists of SET_SIZE buckets, with each bucket containing
 * SET_SLOTS_SIZE slots.
 */
typedef struct {
    uint32_t table[SET_SIZE][SET_SLOTS_SIZE];
} set_t;

/**
 * set_reset - clear a set
 * @set: a pointer points to target set
 */
static inline void set_reset(set_t *set)
{
    memset(set, 0, sizeof(set_t));
}

/**
 * set_add - insert a new element into the set
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
static bool set_add(set_t *set, uint32_t key)
{
    const uint32_t index = HASH(key);
    uint8_t count = 0;
    while (set->table[index][count]) {
        if (set->table[index][count++] == key)
            return false;
    }

    set->table[index][count] = key;
    return true;
}

/**
 * set_has - check whether the element exist in the set or not
 * @set: a pointer points to target set
 * @key: the key of the inserted entry
 */
static bool set_has(set_t *set, uint32_t key)
{
    const uint32_t index = HASH(key);
    for (uint8_t count = 0; set->table[index][count]; count++) {
        if (set->table[index][count] == key)
            return true;
    }
    return false;
}

static void DFS(node_t *root, set_t *node_set)
{
    if (set_has(node_set, root->value->pc_start))
        return;
    set_add(node_set, root->value->pc_start);
    if (root->left)
        DFS(root->left, node_set);
    if (root->right)
        DFS(root->right, node_set);
}

static void DFS_vector(node_t *root, set_t *node_set, vector_t *vec)
{
    if (set_has(node_set, root->value->pc_start))
        return;
    set_add(node_set, root->value->pc_start);
    vec->arr[vec->size++] = root;
    if (root->left)
        DFS_vector(root->left, node_set, vec);
    if (root->right)
        DFS_vector(root->right, node_set, vec);
}

static node_t *build_cfg(block_t *root, set_t *block_set)
{
    if (set_has(block_set, root->pc_start))
        return NULL;
    set_add(block_set, root->pc_start);
    node_t *new_node = malloc(sizeof(node_t));
    new_node->value = root;
    new_node->dom.size = 0;
    if (root->left)
        new_node->left = build_cfg(root->left, block_set);
    else
        new_node->left = NULL;
    if (root->right)
        new_node->right = build_cfg(root->right, block_set);
    else
        new_node->right = NULL;
    return new_node;
}

static void find_dom(node_t *root)
{
    set_t node_set;
    set_reset(&node_set);
    vector_t vec;
    vec.size = 0;
    DFS_vector(root, &node_set, &vec);
    for (int i = 0; i < vec.size; i++) {
        set_t reachable;
        set_reset(&reachable);
        node_t *target = vec.arr[i];
        set_add(&reachable, target->value->pc_start);
        DFS(root, &reachable);
        for (int j = 0; j < vec.size; j++) {
            if (!set_has(&reachable, vec.arr[j]->value->pc_start)) {
                target->dom.arr[target->dom.size++] = vec.arr[j];
            }
        }
    }
}

block_vector_t *detect_loop(block_t *root)
{
    set_t cfg_nodes, remove_nodes;
    set_reset(&cfg_nodes);
    set_reset(&remove_nodes);
    node_t *root_node = build_cfg(root, &cfg_nodes);
    find_dom(root_node);
    for (int i = 0; i < root_node->dom.size; i++) {
        if (root_node->dom.arr[i]->left == root_node ||
            root_node->dom.arr[i]->left == root_node) {
            node_t *target = root_node->dom.arr[i];
            for (int j = 0; j < target->dom.size; j++) {
                set_add(&remove_nodes, target->dom.arr[j]->value->pc_start);
            }
        }
    }
    block_vector_t *block_vec = malloc(sizeof(block_vector_t));
    block_vec->size = 0;
    block_vec->arr[block_vec->size++] = root_node->value;
    for (int i = 0; i < root_node->dom.size; i++) {
        if (!set_has(&remove_nodes, root_node->dom.arr[i]->value->pc_start)) {
            block_vec->arr[block_vec->size++] = root_node->dom.arr[i]->value;
        }
    }
    return block_vec;
}