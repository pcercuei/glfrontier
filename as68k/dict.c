
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "dict.h"

/*#define DEBUG*/

#ifndef MIN
# define MIN(a,b)		((a)<(b) ? (a) : (b))
#endif
#ifndef MAX
# define MAX(a,b)		((a)>(b) ? (a) : (b))
#endif

#define _NONE	-1
#define LEFT	0
#define RIGHT	1
#define _ERR	2

static int parent_side (const struct Node *node)
{
	if (node->parent == NULL)
		return _NONE;
	else if (node->parent->child[LEFT] == node)
		return LEFT;
	else if (node->parent->child[RIGHT] == node)
		return RIGHT;
	else
		return _ERR;
}

static void print_tree (const struct Node *node, int depth)
{
	int i;
	//assert (depth < 10);
	if (depth==0) printf ("Tree printout:\n");
	if (!node) return;
	for (i=0; i<depth; i++) {
		printf ("\t");
	}
	
	switch (parent_side (node)) {
		case _NONE: printf ("+ "); break;
		case LEFT: printf ("L "); break;
		case RIGHT: printf ("R "); break;
		default: printf ("? "); break;
	}
	printf ("\"%s\" bal %d: obj %x\n", node->key, node->balance, (int)node->obj);
	
	if (node->child[LEFT]) print_tree (node->child[LEFT], depth+1);
	if (node->child[RIGHT]) print_tree (node->child[RIGHT], depth+1);
}

static struct Node *alloc_node ()
{
	struct Node *node = (struct Node *) malloc (sizeof (struct Node));
	memset (node, 0, sizeof (struct Node));
	return node;
}

static void free_node (struct Node *node)
{
	if (node->key) free (node->key);
	free (node);
}

static void set_node (struct Node *node, const char *key, void *obj)
{
	node->key = (char *) malloc (strlen(key)+1);
	strcpy (node->key, key);
	node->obj = obj;
}

/*
 * This node is unbalanced :-o
 * Returns new top node.
 */
static struct Node *balance_node (struct Dict *tree, struct Node *high)
{
	struct Node *mid = NULL;
	struct Node *low = NULL;
	
	/* Single, rightwards */
	if ((high->balance == +2) && (high->child[RIGHT]->balance >= 0)) {
		mid = high->child[RIGHT];
		low = mid->child[RIGHT];

		/* this one ends at top */
		mid->parent = high->parent;
		switch (parent_side (high)) {
			case _NONE: tree->root = mid; break;
			case LEFT: mid->parent->child[LEFT] = mid; break;
			case RIGHT: mid->parent->child[RIGHT] = mid; break;
		}

		high->child[RIGHT] = mid->child[LEFT];
		if (mid->child[LEFT]) mid->child[LEFT]->parent = high;
		
		mid->child[LEFT] = high;
		high->parent = mid;
		
		high->balance = high->balance - 1 - MAX (mid->balance, 0);
		mid->balance = mid->balance - 1 + MIN (high->balance, 0);

		return mid;
	}
	/* Single, leftwards */
	else if ((high->balance == -2) && (high->child[LEFT]->balance <= 0)) {
		mid = high->child[LEFT];
		low = mid->child[LEFT];

		/* this one ends at top */
		mid->parent = high->parent;
		switch (parent_side (high)) {
			case _NONE: tree->root = mid; break;
			case LEFT: mid->parent->child[LEFT] = mid; break;
			case RIGHT: mid->parent->child[RIGHT] = mid; break;
		}

		high->child[LEFT] = mid->child[RIGHT];
		if (mid->child[RIGHT]) mid->child[RIGHT]->parent = high;
		
		mid->child[RIGHT] = high;
		high->parent = mid;

		high->balance = high->balance + 1 - MIN (mid->balance, 0);
		mid->balance = mid->balance + 1 + MAX (high->balance, 0);

		return mid;
	}
	/* Double, rightwards */
	else if ((high->balance == +2) && (high->child[RIGHT]->balance == -1)) {
		mid = high->child[RIGHT];
		low = mid->child[LEFT];
		/* this one ends at top */
		low->parent = high->parent;
		switch (parent_side (high)) {
			case _NONE: tree->root = low; break;
			case LEFT: low->parent->child[LEFT] = low; break;
			case RIGHT: low->parent->child[RIGHT] = low; break;
		}

		high->child[RIGHT] = low->child[LEFT];
		if (low->child[LEFT]) low->child[LEFT]->parent = high;
		
		mid->child[LEFT] = low->child[RIGHT];
		if (low->child[RIGHT]) low->child[RIGHT]->parent = mid;
		
		low->child[LEFT] = high;
		high->parent = low;
		
		low->child[RIGHT] = mid;
		mid->parent = low;

		high->balance = -MAX (low->balance, 0);
		mid->balance = -MIN (low->balance, 0);
		low->balance = 0;
		
		return low;
	}
	/* Double, leftwards */
	else if ((high->balance == -2) && (high->child[LEFT]->balance == +1)) {
		mid = high->child[LEFT];
		low = mid->child[RIGHT];

		/* this one ends at top */
		low->parent = high->parent;
		switch (parent_side (high)) {
			case _NONE: tree->root = low; break;
			case LEFT: low->parent->child[LEFT] = low; break;
			case RIGHT: low->parent->child[RIGHT] = low; break;
		}

		high->child[LEFT] = low->child[RIGHT];
		if (low->child[RIGHT]) low->child[RIGHT]->parent = high;
		
		mid->child[RIGHT] = low->child[LEFT];
		if (low->child[LEFT]) low->child[LEFT]->parent = mid;
		
		low->child[RIGHT] = high;
		high->parent = low;
		
		low->child[LEFT] = mid;
		mid->parent = low;

		high->balance = -MIN (low->balance, 0);
		mid->balance = -MAX (low->balance, 0);
		low->balance = 0;

		return low;
	} else {
		assert ("This shouldn't happen in balance_node()");
		return NULL;
	}
}

void dict_init (struct Dict *tree)
{
	tree->root = NULL;
	tree->len = 0;
}

static void node_recurse_free (struct Node *node)
{
	if (node->child[LEFT]) {
		node_recurse_free (node->child[LEFT]);
	}
	if (node->child[RIGHT]) {
		node_recurse_free (node->child[RIGHT]);
	}
	free_node (node);
}

void dict_free (struct Dict *tree)
{
	node_recurse_free (tree->root);
}

void dict_remove (struct Dict *tree, const char *key)
{
	int side;
	struct Node *to_remove;
	struct Node *iter;
	struct Node *rem_pos;
	int rem_dir;

	to_remove = dict_get (tree, key);

	if (to_remove == NULL) return;

	side = parent_side (to_remove);
	
	/* easy. no children */
	if ((to_remove->child[LEFT] == NULL) && (to_remove->child[RIGHT] == NULL)) {
		if (side == _NONE) {
			tree->root = NULL;
		} else {
			to_remove->parent->child[side] = NULL;
		}
		rem_pos = to_remove->parent;
		rem_dir = side;
	} else if (to_remove->child[LEFT] == NULL) {
		/* only righthand child. give it to parent */
		if (side == _NONE) {
			tree->root = to_remove->child[RIGHT];
		} else {
			to_remove->parent->child[side] = to_remove->child[RIGHT];
		}
		to_remove->child[RIGHT]->parent = to_remove->parent;
		
		rem_pos = to_remove->parent;
		rem_dir = side;
	} else if (to_remove->child[RIGHT] == NULL) {
		/* only lefthand child. give it to parent... */
		if (side == _NONE) {
			tree->root = to_remove->child[LEFT];
		} else {
			to_remove->parent->child[side] = to_remove->child[LEFT];
		}
		to_remove->child[LEFT]->parent = to_remove->parent;
		
		rem_pos = to_remove->parent;
		rem_dir = side;
	} else {
		/* 2 children.. more complex. we give the parent the
		 * rightmost child of the left child :-) */
		iter = to_remove->child[LEFT];
		while (iter->child[RIGHT]) {
			iter = iter->child[RIGHT];
		}
		
		/* maybe it has a left child. reparent it if so */
		if (iter->child[LEFT]) {
			iter->child[LEFT]->parent = iter->parent;
			iter->parent->child[parent_side (iter)] = iter->child[LEFT];
			iter->child[LEFT] = NULL;
		} else {
			iter->parent->child[parent_side (iter)] = NULL;
		}
		rem_pos = iter->parent;
		rem_dir = RIGHT;
		if (rem_pos == to_remove) {
			rem_pos = iter;
			rem_dir = LEFT;
		}
		
		switch (parent_side (to_remove)) {
			case _NONE:
				tree->root = iter;
				iter->parent = NULL;
				break;
			case LEFT:
				iter->parent = to_remove->parent;
				iter->parent->child[LEFT] = iter;
				break;
			case RIGHT:
				iter->parent = to_remove->parent;
				iter->parent->child[RIGHT] = iter;
				break;
		}
		
		if ((to_remove->child[LEFT] != NULL) &&
		    (to_remove->child[LEFT] != iter)) {
			iter->child[LEFT] = to_remove->child[LEFT];
			iter->child[LEFT]->parent = iter;
		} else {
			iter->child[LEFT] = NULL;
		}
		if ((to_remove->child[RIGHT] != NULL) &&
		    (to_remove->child[RIGHT] != iter)) {
			iter->child[RIGHT] = to_remove->child[RIGHT];
			iter->child[RIGHT]->parent = iter;
		} else {
			iter->child[RIGHT] = NULL;
		}
		iter->balance = to_remove->balance;
	}
	/* Iter should now be parent of [re]moved node.
	 * we need to recalculate balance */
	iter = rem_pos;
	assert (iter != to_remove);
	/* AVL rotate if required */
	/* Go back through parents seeing if some cunt is fucked */
	while (iter) {
		switch (rem_dir) {
			case LEFT: side = -1; break;
			case RIGHT: side = 1; break;
		}
		iter->balance -= side;
		
		/* unbalanced node */
		if (abs (iter->balance) >= 2) {
			iter = balance_node (tree, iter);
			if (iter->balance != 0) return;
		} else if (abs (iter->balance) == 1) {
			break;
		}
		rem_dir = parent_side (iter);
		iter = iter->parent;
	}
	free_node (to_remove);
}

struct Node *dict_get (struct Dict *tree, const char *key)
{
	int cmp;
	struct Node *parent = NULL;
	struct Node *node;

	if (tree->root == NULL) {
		return NULL;
	}

	node = tree->root;
	while (1) {
		/* Not found */
		if ((parent) && (node == NULL))
			return NULL;
		
		cmp = strcmp (node->key, key);

		if (cmp < 0) {
			parent = node;
			node = node->child[LEFT];
		} else if (cmp > 0) {
			parent = node;
			node = node->child[RIGHT];
		} else {
			/* match */
			return node;
		}
	}


}

/*
 * Returns 1 if key is new, otherwise zero.
 */
int dict_set (struct Dict *tree, const char *key, void *obj)
{
	int cmp = 0;
	int side;
	struct Node *parent = NULL;
	struct Node *node;

	if (tree->root == NULL) {
		tree->root = alloc_node ();
		set_node (tree->root, key, obj);
		tree->len++;
		return 1;
	}

	node = tree->root;
	while (1) {
		/* Found adding position */
		if ((parent) && (node == NULL))
			break;
		cmp = strcmp (node->key, key);

		if (cmp < 0) {
			parent = node;
			node = node->child[LEFT];
		} else if (cmp > 0) {
			parent = node;
			node = node->child[RIGHT];
		} else {
			/* match */
			node->obj = obj;
			return 0;
		}
	}

	/* add new */
	node = alloc_node ();
	set_node (node, key, obj);
	tree->len++;
	if (cmp < 0) {
		parent->child[LEFT] = node;
	} else {
		parent->child[RIGHT] = node;
	}
	node->parent = parent;
	
	/* AVL rotate if required */
	/* Go back through parents seeing if some cunt is fucked */
	while (parent) {
		if (parent->child[LEFT] == node) {
			side = -1;
		} else {
			side = +1;
		}
		
		parent->balance += side;
		
		/* unbalanced node */
		if (abs (parent->balance) >= 2) {
			parent = balance_node (tree, parent);
			if (parent->balance == 0) return 1;
		} else if (parent->balance == 0) {
			break;
		}
		node = parent;
		parent = node->parent;
	}
	/* nothing unbalanced */
	return 1;
}

#ifdef DEBUG
/*
 * Asserts if the tree's balance is fucked in some way.
 */
static int node_isbalanced (struct Node *node)
{
	/* heights of trees */
	int left = 0;
	int right = 0;

	if (node->child[LEFT]) {
		left += 1 + abs (node_isbalanced (node->child[LEFT]));
	}
	if (node->child[RIGHT]) {
		right += 1 + abs (node_isbalanced (node->child[RIGHT]));
	}

	//printf ("Node '%s' has height %d, balance %d (claims %d)\n", node->key, MAX (left, right), right-left, node->balance);
	
	assert ((right-left) == node->balance);
	assert (abs (right-left) < 2);

	return MAX (left, right);
}

#define TEST_SIZE	40

int main (void)
{
	int i;
	int errs;
	char buf[20];
	char *keys[TEST_SIZE];
	void *objs[TEST_SIZE];
	struct Dict t;
	struct Node *n;

	srand (4);
	dict_init (&t);

	for (i=0; i<TEST_SIZE; i++) {
		/* Make key */
		sprintf(buf, "Node%d:%02d", rand(), i);
		keys[i] = (char *) malloc (strlen(buf)+1);
		strcpy (keys[i], buf);
		/* Data */
		objs[i] = (void *) rand();
		
		dict_set (&t, keys[i], objs[i]);
	}

	print_tree (t.root, 0);
	node_isbalanced (t.root);
	
	for (i=0, errs=0; i<TEST_SIZE; i++) {
		/* Check if returned keys are correct */
		n = dict_get (&t, keys[i]);
		if ((n == NULL) || (n->obj != objs[i])) {
			errs++;
		}
	}
	printf ("%d incorrect gets.\n", errs);
	
	printf ("Removing half the keys...\n");
	/* now remove half of them */
	for (i=0; i<6*TEST_SIZE/8; i++) {
		dict_remove (&t, keys[i]);
	}
	print_tree (t.root, 0);

	for (i=6*TEST_SIZE/8, errs=0; i<TEST_SIZE; i++) {
		/* Check if returned keys are correct */
		n = dict_get (&t, keys[i]);
		if ((n == NULL) || (n->obj != objs[i])) {
			errs++;
		}
	}
	printf ("%d incorrect gets.\n", errs);
	node_isbalanced (t.root);
	
	dict_free (&t);
	
	return 0;
}
#endif /* DEBUG */

