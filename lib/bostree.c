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

#include "bostree.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Tree structure */
struct _BOSTree {
	BOSNode *root_node;

	BOSTree_cmp_function cmp_function;
	BOSTree_free_function free_function;
};

/* Local helper functions */
static int _imax(const int i1, const int i2) {
	return i1 > i2 ? i1 : i2;
}

static int _bostree_balance(BOSNode *node) {
	const int left_depth = node->left_child_node ? node->left_child_node->depth + 1 : 0;
	const int right_depth = node->right_child_node ? node->right_child_node->depth + 1 : 0;
	return right_depth - left_depth;
}

static BOSNode *_bostree_rotate_right(BOSTree *tree, BOSNode *P) {
	// Rotate right:
	//
	//      P                     L
	//  L        R     -->    c1      P
	//c1 c2                        c2     R
	//
	BOSNode *L = P->left_child_node;

	if(P->parent_node) {
		if(P->parent_node->left_child_node == P) {
			P->parent_node->left_child_node = L;
		}
		else {
			P->parent_node->right_child_node = L;
		}
	}
	else {
		tree->root_node = L;
	}

	L->parent_node = P->parent_node;

	P->left_child_node = L->right_child_node;
	P->left_child_count = L->right_child_count;
	if(P->left_child_node) {
		P->left_child_node->parent_node = P;
	}
	P->depth = _imax(P->left_child_node ? 1 + P->left_child_node->depth : 0, P->right_child_node ? 1 + P->right_child_node->depth : 0);
	P->parent_node = L;

	L->right_child_node = P;
	P->parent_node = L;
	L->right_child_count = P->left_child_count + P->right_child_count + 1;
	L->depth = _imax(L->left_child_node ? 1 + L->left_child_node->depth : 0, L->right_child_node ? 1 + L->right_child_node->depth : 0);

	return L;
}

static BOSNode *_bostree_rotate_left(BOSTree *tree, BOSNode *P) {
	// Rotate left:
	//
	//      P                     R
	//  L        R     -->    P      c2
	//         c1 c2        L  c1
	//
	BOSNode *R = P->right_child_node;

	if(P->parent_node) {
		if(P->parent_node->left_child_node == P) {
			P->parent_node->left_child_node = R;
		}
		else {
			P->parent_node->right_child_node = R;
		}
	}
	else {
		tree->root_node = R;
	}

	R->parent_node = P->parent_node;

	P->right_child_node = R->left_child_node;
	P->right_child_count = R->left_child_count;
	if(P->right_child_node) {
		P->right_child_node->parent_node = P;
	}
	P->depth = _imax(P->left_child_node ? 1 + P->left_child_node->depth : 0, P->right_child_node ? 1 + P->right_child_node->depth : 0);
	P->parent_node = R;

	R->left_child_node = P;
	P->parent_node = R;
	R->left_child_count = P->left_child_count + P->right_child_count + 1;
	R->depth = _imax(R->left_child_node ? 1 + R->left_child_node->depth : 0, R->right_child_node ? 1 + R->right_child_node->depth : 0);

	return R;
}


/* API implementation */
BOSTree *bostree_new(BOSTree_cmp_function cmp_function, BOSTree_free_function free_function) {
	BOSTree *new_tree = malloc(sizeof(BOSTree));
	new_tree->root_node = NULL;
	new_tree->cmp_function = cmp_function;
	new_tree->free_function = free_function;
	return new_tree;
}

void bostree_destroy(BOSTree *tree) {
	// Walk the tree and unref all nodes
	BOSNode *node = tree->root_node;
	while(node) {
		// The order should not really matter, but use post-order traversal here.
		while(node->left_child_node) {
			node = node->left_child_node;
		}

		if(node->right_child_node) {
			node = node->right_child_node;
			continue;
		}

		// At this point, we can be sure that this node has no child nodes.
		// So it is safe to remove it.
		BOSNode *next = node->parent_node;
		if(next) {
			if(next->left_child_node == node) {
				next->left_child_node = NULL;
			}
			else {
				next->right_child_node = NULL;
			}
		}
		bostree_node_weak_unref(tree, node);
		node = next;
	}

	free(tree);
}

unsigned int bostree_node_count(BOSTree *tree) {
	return tree->root_node ? tree->root_node->left_child_count + tree->root_node->right_child_count + 1 : 0;
}

BOSNode *bostree_insert(BOSTree *tree, void *key, void *data) {
	BOSNode **node = &tree->root_node;
	BOSNode *parent_node = NULL;

	// Find tree position to insert new node
	while(*node) {
		parent_node = *node;
		int cmp = tree->cmp_function(key, (*node)->key);
		if(cmp < 0) {
			(*node)->left_child_count++;
			node = &(*node)->left_child_node;
		}
		else {
			(*node)->right_child_count++;
			node = &(*node)->right_child_node;
		}
	}

	// Create new node
	BOSNode *new_node = malloc(sizeof(BOSNode));
	memset(new_node, 0, sizeof(BOSNode));
	new_node->key = key;
	new_node->data = data;
	new_node->weak_ref_count = 1;
	new_node->weak_ref_node_valid = 1;
	new_node->parent_node = parent_node;

	*node = new_node;

	if(!parent_node) {
		// Simple case, this is the first node.
		tree->root_node = new_node;
		return new_node;
	}

	// Check if the depth changed with the new node:
	// It does only change if this is the first child of the parent
	if(!!parent_node->left_child_node ^ !!parent_node->right_child_node) {
		// Bubble the information up the tree
		parent_node->depth++;
		while(parent_node->parent_node) {
			// Assign new depth
			parent_node = parent_node->parent_node;

			unsigned int new_left_depth  = parent_node->left_child_node ? parent_node->left_child_node->depth + 1 : 0;
			unsigned int new_right_depth = parent_node->right_child_node ? parent_node->right_child_node->depth + 1 : 0;

			unsigned int max_depth = _imax(new_left_depth, new_right_depth);

			if(parent_node->depth != max_depth) {
				parent_node->depth = max_depth;
			}
			else {
				// We can break here, because if the depth did not change
				// at this level, it won't have further up either.
				break;
			}

			// Check if this node violates the AVL property, that is, that the
			// depths differ by no more than 1.
			if(new_left_depth - 2 == new_right_depth) {
				// Handle left-right case
				if(_bostree_balance(parent_node->left_child_node) > 0) {
					_bostree_rotate_left(tree, parent_node->left_child_node);
				}

				// Left is two levels deeper than right. Rotate right.
				parent_node = _bostree_rotate_right(tree, parent_node);
			}
			else if(new_left_depth + 2 == new_right_depth) {
				// Handle right-left case
				if(_bostree_balance(parent_node->right_child_node) < 0) {
					_bostree_rotate_right(tree, parent_node->right_child_node);
				}

				// Right is two levels deeper than left. Rotate left.
				parent_node = _bostree_rotate_left(tree, parent_node);
			}
		}
	}

	return new_node;
}

void bostree_remove(BOSTree *tree, BOSNode *node) {
	BOSNode *bubble_up = NULL;

	// If this node has children on both sides, bubble one of it upwards
	// and rotate within the subtrees.
	if(node->left_child_node && node->right_child_node) {
		BOSNode *candidate = NULL;
		BOSNode *lost_child = NULL;
		if(node->left_child_node->depth >= node->right_child_node->depth) {
			// Left branch is deeper than right branch, might be a good idea to
			// bubble from this side to maintain the AVL property with increased
			// likelihood.
			node->left_child_count--;
			candidate = node->left_child_node;
			while(candidate->right_child_node) {
				candidate->right_child_count--;
				candidate = candidate->right_child_node;
			}
			lost_child = candidate->left_child_node;
		}
		else {
			node->right_child_count--;
			candidate = node->right_child_node;
			while(candidate->left_child_node) {
				candidate->left_child_count--;
				candidate = candidate->left_child_node;
			}
			lost_child = candidate->right_child_node;
		}

		BOSNode *bubble_start = candidate->parent_node;
		if(bubble_start->left_child_node == candidate) {
			bubble_start->left_child_node = lost_child;
		}
		else {
			bubble_start->right_child_node = lost_child;
		}
		if(lost_child) {
			lost_child->parent_node = bubble_start;
		}

		// We will later rebalance upwards from bubble_start up to candidate.
		// But first, anchor candidate into the place where "node" used to be.

		if(node->parent_node) {
			if(node->parent_node->left_child_node == node) {
				node->parent_node->left_child_node = candidate;
			}
			else {
				node->parent_node->right_child_node = candidate;
			}
		}
		else {
			tree->root_node = candidate;
		}
		candidate->parent_node = node->parent_node;

		candidate->left_child_node = node->left_child_node;
		candidate->left_child_count = node->left_child_count;
		candidate->right_child_node = node->right_child_node;
		candidate->right_child_count = node->right_child_count;

		if(candidate->left_child_node) {
			candidate->left_child_node->parent_node = candidate;
		}

		if(candidate->right_child_node) {
			candidate->right_child_node->parent_node = candidate;
		}

		// From here on, node is out of the game.
		// Rebalance up to candidate.

		if(bubble_start != node) {
			while(bubble_start != candidate) {
				bubble_start->depth = _imax((bubble_start->left_child_node ? bubble_start->left_child_node->depth + 1 : 0),
					(bubble_start->right_child_node ? bubble_start->right_child_node->depth + 1 : 0));
				int balance = _bostree_balance(bubble_start);
				if(balance > 1) {
					// Rotate left. Check for right-left case before.
					if(_bostree_balance(bubble_start->right_child_node) < 0) {
						_bostree_rotate_right(tree, bubble_start->right_child_node);
					}
					bubble_start = _bostree_rotate_left(tree, bubble_start);
				}
				else if(balance < -1) {
					// Rotate right. Check for left-right case before.
					if(_bostree_balance(bubble_start->left_child_node) > 0) {
						_bostree_rotate_left(tree, bubble_start->left_child_node);
					}
					bubble_start = _bostree_rotate_right(tree, bubble_start);
				}
				bubble_start = bubble_start->parent_node;
			}
		}

		// Fixup candidate's depth
		candidate->depth = _imax((candidate->left_child_node ? candidate->left_child_node->depth + 1 : 0),
			(candidate->right_child_node ? candidate->right_child_node->depth + 1 : 0));

		// We'll have to fixup child counts and depths up to the root, do that
		// later.
		bubble_up = candidate->parent_node;

		// Fix immediate parent node child count here.
		if(bubble_up) {
			if(bubble_up->left_child_node == candidate) {
				bubble_up->left_child_count--;
			}
			else {
				bubble_up->right_child_count--;
			}
		}
	}
	else {
		// If this node has children on one side only, removing it is much simpler.
		if(!node->parent_node) {
			// Simple case: Node _was_ the old root.
			if(node->left_child_node) {
				tree->root_node = node->left_child_node;
				if(node->left_child_node) {
					node->left_child_node->parent_node = NULL;
				}
			}
			else {
				tree->root_node = node->right_child_node;
				if(node->right_child_node) {
					node->right_child_node->parent_node = NULL;
				}
			}

			// No rebalancing to do
			bubble_up = NULL;
		}
		else {
			BOSNode *candidate = node->left_child_node;
			int candidate_count = node->left_child_count;
			if(node->right_child_node) {
				candidate = node->right_child_node;
				candidate_count = node->right_child_count;
			}

			if(node->parent_node->left_child_node == node) {
				node->parent_node->left_child_node = candidate;
				node->parent_node->left_child_count = candidate_count;
			}
			else {
				node->parent_node->right_child_node = candidate;
				node->parent_node->right_child_count = candidate_count;
			}

			if(candidate) {
				candidate->parent_node = node->parent_node;
			}

			// Again, from here on, the original node is out of the game.
			// Rebalance up to the root.
			bubble_up = node->parent_node;
		}
	}

	// At this point, everything below and including bubble_start is
	// balanced, and we have to look further up.

	char bubbling_finished = 0;
	while(bubble_up) {
		if(!bubbling_finished) {
			// Calculate updated depth for bubble_up
			unsigned int left_depth = bubble_up->left_child_node ? bubble_up->left_child_node->depth + 1 : 0;
			unsigned int right_depth = bubble_up->right_child_node ? bubble_up->right_child_node->depth + 1 : 0;
			unsigned int new_depth = _imax(left_depth, right_depth);
			char depth_changed = (new_depth != bubble_up->depth);
			bubble_up->depth = new_depth;

			// Rebalance bubble_up
			// Not necessary for the first node, but calling _bostree_balance once
			// isn't that much overhead.
			int balance = _bostree_balance(bubble_up);
			if(balance < -1) {
				if(_bostree_balance(bubble_up->left_child_node) > 0) {
					_bostree_rotate_left(tree, bubble_up->left_child_node);
				}
				bubble_up = _bostree_rotate_right(tree, bubble_up);
			}
			else if(balance > 1) {
				if(_bostree_balance(bubble_up->right_child_node) < 0) {
					_bostree_rotate_right(tree, bubble_up->right_child_node);
				}
				bubble_up = _bostree_rotate_left(tree, bubble_up);
			}
			else {
				if(!depth_changed) {
					// If we neither had to rotate nor to change the depth,
					// then we are obviously finished.  Only update child
					// counts from here on.
					bubbling_finished = 1;
				}
			}
		}

		if(bubble_up->parent_node) {
			if(bubble_up->parent_node->left_child_node == bubble_up) {
				bubble_up->parent_node->left_child_count--;
			}
			else {
				bubble_up->parent_node->right_child_count--;
			}
		}
		bubble_up = bubble_up->parent_node;
	}

	node->weak_ref_node_valid = 0;
	bostree_node_weak_unref(tree, node);
}

BOSNode *bostree_node_weak_ref(BOSNode *node) {
	assert(node->weak_ref_count < 127);
	assert(node->weak_ref_count > 0);
	node->weak_ref_count++;
	return node;
}

BOSNode *bostree_node_weak_unref(BOSTree *tree, BOSNode *node) {
	node->weak_ref_count--;
	if(node->weak_ref_count == 0) {
		if(tree->free_function) {
			tree->free_function(node);
		}
		free(node);
	}
	else if(node->weak_ref_node_valid) {
		return node;
	}
	return NULL;
}

BOSNode *bostree_lookup(BOSTree *tree, const void *key) {
	BOSNode *node = tree->root_node;
	while(node) {
		int cmp = tree->cmp_function(key, node->key);
		if(cmp == 0) {
			break;
		}
		else if(cmp < 0) {
			node = node->left_child_node;
		}
		else {
			node = node->right_child_node;
		}
	}
	return node;
}

BOSNode *bostree_select(BOSTree *tree, unsigned int index) {
	BOSNode *node = tree->root_node;
	while(node) {
		if(node->left_child_count <= index) {
			index -= node->left_child_count;
			if(index == 0) {
				return node;
			}
			index--;
			node = node->right_child_node;
		}
		else {
			node = node->left_child_node;
		}
	}
	return node;
}

BOSNode *bostree_next_node(BOSNode *node) {
	if(node->right_child_node) {
		node = node->right_child_node;
		while(node->left_child_node) {
			node = node->left_child_node;
		}
		return node;
	}
	else if(node->parent_node) {
		while(node->parent_node && node->parent_node->right_child_node == node) {
			node = node->parent_node;
		}
		return node->parent_node;
	}
	return NULL;
}

BOSNode *bostree_previous_node(BOSNode *node) {
	if(node->left_child_node) {
		node = node->left_child_node;
		while(node->right_child_node) {
			node = node->right_child_node;
		}
		return node;
	}
	else if(node->parent_node) {
		while(node->parent_node && node->parent_node->left_child_node == node) {
			node = node->parent_node;
		}
		return node->parent_node;
	}
	return NULL;
}

unsigned int bostree_rank(BOSNode *node) {
	unsigned int counter = node->left_child_count;
	while(node) {
		if(node->parent_node && node->parent_node->right_child_node == node) counter += 1 + node->parent_node->left_child_count;
		node = node->parent_node;
	}
	return counter;
}

#if !defined(NDEBUG)
#include <stdio.h>
#include <unistd.h>

/* Debug helpers:

	Print the tree to stdout in dot format.
*/

static void _bostree_print_helper(BOSNode *node) {
	printf("  %s [label=\"\\N (%d,%d,%d)\"];\n", (char *)node->key, node->left_child_count, node->right_child_count, node->depth);
	if(node->parent_node) {
		printf("  %s -> %s [color=green];\n", (char *)node->key, (char *)node->parent_node->key);
	}

	if(node->left_child_node != NULL) {
		printf("  %s -> %s\n", (char *)node->key, (char *)node->left_child_node->key);
		_bostree_print_helper(node->left_child_node);
	}
	if(node->right_child_node != NULL) {
		printf("  %s -> %s\n", (char *)node->key, (char *)node->right_child_node->key);
		_bostree_print_helper(node->right_child_node);
	}
}

void bostree_print(BOSTree *tree) {
	if(tree->root_node == NULL) {
		return;
	}

	printf("digraph {\n  ordering = out;\n");
	_bostree_print_helper(tree->root_node);
	printf("}\n");
	fsync(0);
}
#endif
