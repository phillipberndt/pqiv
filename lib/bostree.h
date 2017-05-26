/*
	Self-Balancing order statistic tree

	Implements an AVL tree with two additional methods,
	Select(i), which finds the i'th smallest element, and
	Rank(x), which returns the rank of a given element,
	which both run in O(log n).

	The tree is implemented with map semantics, that is, there are separete key
	and value pointers.

	Copyright (c) 2017, Phillip Berndt

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BOSTREE_H
#define BOSTREE_H

/* Opaque tree structure */
typedef struct _BOSTree BOSTree;

/* Node structure */
struct _BOSNode {
	unsigned int left_child_count;
	unsigned int right_child_count;
	unsigned int depth;

	struct _BOSNode *left_child_node;
	struct _BOSNode *right_child_node;
	struct _BOSNode *parent_node;

	void *key;
	void *data;

	unsigned char weak_ref_count;
	unsigned char weak_ref_node_valid;
};
typedef struct _BOSNode BOSNode;

/*
	Public interface
*/

/**
 * Key comparison function.
 *
 * Should return a positive value if the second argument is larger than the
 * first one, a negative value if the first is larger, and zero exactly if both
 * are equal.
 */
typedef int (*BOSTree_cmp_function)(const void *, const void *);

/**
 * Free function for deleted nodes.
 *
 * This function should free the key and data members of a node. The node
 * structure itself is free()d internally by BOSZTree.
 */
typedef void (*BOSTree_free_function)(BOSNode *node);

/**
 * Create a new tree.
 *
 * The cmp_function is mandatory, but for the free function, you may supply a
 * NULL pointer if you do not have any data that needs to be free()d in
 * ->key and ->data.
 */
BOSTree *bostree_new(BOSTree_cmp_function cmp_function, BOSTree_free_function free_function);

/**
 * Destroy a tree and all its members.
 */
void bostree_destroy(BOSTree *tree);

/**
 * Return the number of nodes in a tree
 */
unsigned int bostree_node_count(BOSTree *tree);

/**
 * Insert a new member into the tree and return the associated node.
 */
BOSNode *bostree_insert(BOSTree *tree, void *key, void *data);

/**
 * Remove a given node from a tree.
 */
void bostree_remove(BOSTree *tree, BOSNode *node);

/**
 * Weak reference management for nodes.
 *
 * Nodes have an internal reference counter. They are only free()d after the
 * last weak reference has been removed. Calling bostree_node_weak_unref() on a
 * node which has been removed from the tree results in the weak reference
 * count being decreased, the node being possibly free()d if this has been the
 * last weak reference, and a NULL pointer being returned.
 */
BOSNode *bostree_node_weak_ref(BOSNode *node);

/**
 * Weak reference management for nodes.
 * See bostree_node_weak_ref()
 */
BOSNode *bostree_node_weak_unref(BOSTree *tree, BOSNode *node);

/**
 * Return a node given a key. NULL is returned if a key is not present in the
 * tree.
 */
BOSNode *bostree_lookup(BOSTree *tree, const void *key);

/**
 * Return a node given an index in in-order traversal. Indexing starts at 0.
 */
BOSNode *bostree_select(BOSTree *tree, unsigned int index);

/**
 * Return the next node in in-order traversal, or NULL is node was the last
 * node in the tree.
 */
BOSNode *bostree_next_node(BOSNode *node);

/**
 * Return the previous node in in-order traversal, or NULL is node was the first
 * node in the tree.
 */
BOSNode *bostree_previous_node(BOSNode *node);

/**
 * Return the rank of a node within it's owning tree.
 *
 * bostree_select(bostree_rank(node)) == node is always true.
 */
unsigned int bostree_rank(BOSNode *node);

#if !defined(NDEBUG) && (_BSD_SOURCE || _XOPEN_SOURCE || _POSIX_C_SOURCE >= 200112L)
void bostree_print(BOSTree *tree);
#define bostree_debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define bostree_debug(...) void
#endif

#endif
