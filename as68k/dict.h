
#ifndef __DICT_H
#define __DICT_H

typedef struct Dict Dict;

struct Node {
	struct Node *parent;
	struct Node *child[2];
	char balance; /* +ve for right */
	char *key;
	void *obj;
};

struct Dict {
	struct Node *root;
	int len;
};

void dict_init (struct Dict *tree);
void dict_free (struct Dict *tree);
struct Node *dict_get (struct Dict *tree, const char *key);
void dict_remove (struct Dict *tree, const char *key);
int dict_set (struct Dict *tree, const char *key, void *obj);

#endif /* __DICT_H */

