/*
	Self-Balancing order statistic tree

	Implements an AVL tree with two additional methods,
	Select(i), which finds the i'th smallest element, and
	Rank(x), which returns the rank of a given element,
	which both run in O(log n).

	Copyright (c) 2013, Phillip Berndt

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

	unsigned char weak_ref_count      : 7;
	unsigned char weak_ref_node_valid : 1;
};
typedef struct _BOSNode BOSNode;

/*
	Public interface

	See bostree.c for documentation.
*/

typedef int (*BOSTree_cmp_function)(const void *, const void *);
typedef void (*BOSTree_free_function)(BOSNode *node);

BOSTree *bostree_new(BOSTree_cmp_function cmp_function, BOSTree_free_function free_function);
void bostree_destroy(BOSTree *tree);

unsigned int bostree_node_count(BOSTree *tree);

BOSNode *bostree_insert(BOSTree *tree, void *key, void *data);
void bostree_remove(BOSTree *tree, BOSNode *node);

BOSNode *bostree_node_weak_ref(BOSNode *node);
BOSNode *bostree_node_weak_unref(BOSTree *tree, BOSNode *node);

BOSNode *bostree_lookup(BOSTree *tree, void *key);
BOSNode *bostree_select(BOSTree *tree, unsigned int index);

BOSNode *bostree_next_node(BOSNode *node);
BOSNode *bostree_previous_node(BOSNode *node);
unsigned int bostree_rank(BOSNode *node);

#if !defined(NDEBUG) && (_BSD_SOURCE || _XOPEN_SOURCE || _POSIX_C_SOURCE >= 200112L)
void bostree_print(BOSTree *tree);
#endif

#endif
