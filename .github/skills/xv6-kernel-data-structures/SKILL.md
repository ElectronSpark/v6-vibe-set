---
name: xv6-kernel-data-structures
description: 'Use when: working on xv6-os list, llist, hlist, bintree, rbtree, maple tree, xarray, intrusive containers, range maps, indexed object maps, or container lifetime bugs.'
argument-hint: 'Describe the data structure or invariant issue'
---

# xv6 Kernel Data Structures

## When to Use

- Intrusive list, hlist, tree, maple tree, or xarray state is corrupt or misused.
- A subsystem needs ordered lookup, range lookup, sparse integer IDs, or hash-bucket lists.
- You are debugging lifetime bugs caused by removing nodes from shared containers.

## Source Map

- Lists: `kernel/kernel/inc/list.h`, `list_type.h`, `llist.h`.
- Hash lists: `kernel/kernel/hlist.c`, `kernel/kernel/inc/hlist*.h`.
- Trees: `bintree.c`, `rbtree.c`, `kernel/kernel/inc/bintree*.h`, `rbtree.h`.
- Range/index maps: `maple_tree.c`, `xarray.c`, `kernel/kernel/inc/maple_tree*.h`, `xarray*.h`.

## Workflow

1. Identify ownership: which object embeds the node and which lock/RCU rule protects it.
2. Check insert/remove symmetry and whether removal happens before free.
3. Use rb-tree for balanced ordered lookup, maple tree for range/sparse mappings, and xarray for integer-indexed objects.
4. For RCU-visible containers, pair with `xv6-kernel-locking-rcu` before changing deletion.
5. Keep container changes local to the subsystem invariant that needs them.

## Pitfalls

- Intrusive nodes cannot be in two containers at once unless they have separate node fields.
- Container corruption often first appears in unrelated traversal code.
- RCU readers can still see removed objects until the grace period ends.
