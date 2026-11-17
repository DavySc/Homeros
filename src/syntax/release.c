/* ts_tree_delete in steps.  Freeing the old tree after a reparse is one
   uninterrupted walk: on coding.c (errors stop tree-sitter reusing nodes)
   0.8 ms per key on average and up to 4.8 ms, measured; most of the
   stretches without a stop ask.  This is tree-sitter's ts_tree_delete and
   ts_subtree_release (tree.c, subtree.c) with the stack kept between calls,
   so the walk can stop for input; tree-sitter has no public call for it.
   One stack for every Syntax: there is one event loop. */
#include "atomic.h"
#include "subtree.h"
#include "tree.h"

int syntax_release(TSTree *t, unsigned n);

static MutableSubtreeArray stack = array_new();

/* Adds t's nodes (if t) and frees up to n of them; nonzero while some remain. */
int syntax_release(TSTree *t, unsigned n)
{
	if (t) {
		Subtree r = t->root;
		if (!r.data.is_inline && atomic_dec((volatile uint32_t *)&r.ptr->ref_count) == 0)
			array_push(&stack, ts_subtree_to_mut_unsafe(r));
		ts_language_delete(t->language);
		ts_free(t->included_ranges);
		ts_free(t);
	}
	for (; stack.size && n; n--) {
		MutableSubtree x = array_pop(&stack);
		if (x.ptr->child_count) {
			Subtree *c = ts_subtree_children(x);
			for (uint32_t i = 0; i < x.ptr->child_count; i++)
				if (!c[i].data.is_inline && atomic_dec((volatile uint32_t *)&c[i].ptr->ref_count) == 0)
					array_push(&stack, ts_subtree_to_mut_unsafe(c[i]));
			ts_free(c);
		} else {
			if (x.ptr->has_external_tokens) ts_external_scanner_state_delete(&x.ptr->external_scanner_state);
			ts_free(x.ptr);
		}
	}
	return stack.size != 0;
}
