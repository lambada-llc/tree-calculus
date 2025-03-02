# Reduction rules (triage calculus)

Tree calculus does not necessarily prescibe a specific set of reduction rules and is really a family of calculi as [Barry describes in this blog post](https://github.com/barry-jay-personal/blog/blob/main/2024-12-12-calculus-calculi.md).
Check out [his book](https://github.com/barry-jay-personal/tree-calculus/blob/master/tree_book.pdf) for the _original rules_.

In 2024, Johannes suggested the rules presented here and used throughout this repo. They are a bit easier to motivate (rules 1 and 2 are similar to K and S, rules 3 encapsulate triage) and tend to lead to smaller programs and reduction in fewer steps. We also call tree calculus with these particular rules _triage calculus_.

$$
\begin{alignat*}{6}
& \triangle\ & &\triangle        &&\ y\ & &z                && \longrightarrow     y                       && (1)\\
& \triangle\ &(&\triangle\ x)    &&\ y\ & &z                && \longrightarrow     x\ z\ (y\ z) \quad\quad && (2)\\
& \triangle\ &(&\triangle\ w\ x) &&\ y\ & &\triangle        && \longrightarrow     w                       && (3a)\\
& \triangle\ &(&\triangle\ w\ x) &&\ y\ &(&\triangle\ u)    && \longrightarrow     x\ u                    && (3b)\\
& \triangle\ &(&\triangle\ w\ x) &&\ y\ &(&\triangle\ u\ v) && \longrightarrow     y\ u\ v                 && (3c)
\end{alignat*}
$$

The following visualizations have also been used [here](https://olydis.medium.com/a-visual-introduction-to-tree-calculus-2f4a34ceffc2).

## Implicit applications

Applications are not represented explicitly, reducible subtrees are non-binary trees.
The rules describe how to act on nodes with more than two children.

| Rule | Before                   |                  | After                     |
| -----| ------------------------ | ---------------- | ------------------------- |
| (1)  | ![image](imp-1-pre.svg)  | &LongRightArrow; | ![image](imp-1-post.svg)  |
| (2)  | ![image](imp-2-pre.svg)  | &LongRightArrow; | ![image](imp-2-post.svg)  |
| (3a) | ![image](imp-3a-pre.svg) | &LongRightArrow; | ![image](imp-3a-post.svg) |
| (3b) | ![image](imp-3b-pre.svg) | &LongRightArrow; | ![image](imp-3b-post.svg) |
| (3c) | ![image](imp-3c-pre.svg) | &LongRightArrow; | ![image](imp-3c-post.svg) |

## Explicit applications

Applications are represented explicitly as hollow nodes.
The rules describe how to eliminate those application nodes.
Note that application nodes aside, trees are always irreducible values (binary trees).

| Rule | Before                   |                  | After                     |
| -----| ------------------------ | ---------------- | ------------------------- |
| (0a) | ![image](exp-0a-pre.svg) | &LongRightArrow; | ![image](exp-0a-post.svg) |
| (0b) | ![image](exp-0b-pre.svg) | &LongRightArrow; | ![image](exp-0b-post.svg) |
| (1)  | ![image](exp-1-pre.svg)  | &LongRightArrow; | ![image](exp-1-post.svg)  |
| (2)  | ![image](exp-2-pre.svg)  | &LongRightArrow; | ![image](exp-2-post.svg)  |
| (3a) | ![image](exp-3a-pre.svg) | &LongRightArrow; | ![image](exp-3a-post.svg) |
| (3b) | ![image](exp-3b-pre.svg) | &LongRightArrow; | ![image](exp-3b-post.svg) |
| (3c) | ![image](exp-3c-pre.svg) | &LongRightArrow; | ![image](exp-3c-post.svg) |
