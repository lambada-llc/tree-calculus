# Representing data as trees
[Reduction rules](../reduction-rules) imply how to represent _logic_, for instance $△ (△ (△ △)) △$ is an identity program.
However, how to represent traditional _data_ such as numbers or text, is up to us.

## Extensional
Whatever works in λ-calculus and combinatory logic also works in tree calculus.
[Church encoding](https://en.wikipedia.org/wiki/Church_encoding) or [Scott encoding](https://en.wikipedia.org/wiki/Mogensen%E2%80%93Scott_encoding) produce certain terms and their values can be deduced by observing how they _behave_. For instance, it is typical to define _true_ as $λa.λb.a$ (in tree calculus: $△ △$) and _false_ as $λa.λb.b$ (in tree calculus: $△ △ (△ (△ (△ △)) △)$ ). Booleans encoded this way can then be distinguished by applying them to two arguments, effectively a "then" branch and "else" branch.

Note that extensional encodings like this tend to result in one and the same value being representable by infinitely many (internally different) terms. See [this demo](https://treecalcul.us/live/?example=bootstrap-basics) for a demonstration of this fact using Church encodings of the number 3.

## Intensional
Since tree calculus is intensional, programs can directly observe the tree structure of values (including programs).
This allows us to use trees to represent values directly.

For example:
* Booleans: It seems reasonable to represent _false_ and _true_ using $△$ and $△ △$, respectively. These are the two smallest trees and can be distinguished with a single triage reduction.
* Small natural numbers can be represented as $△^n△$, meaning $0$ is just a node $△$ and $n+1$ is a stem $△ n$. Visualized, these "trees" are just chains with a length of $n$ edges.
* Lists can be defined in the usual algebraic style, with empty lists represented by a node $△$ and non-empty lists by a fork $△\ hd\ tl$.
* Large natural numbers can be represented by their binary encoding, as a list of booleans.
* Text can be represented as a list of natural numbers that are for instance the code of each Unicode character.
* Option types can use a leaf for no value and a stem for some value.
* Fixed-size tuples can be represented by connecting the tuple's values using forks, yielding logarithmic time complexity for primitive operations on a tuple. For instance, tuple $(a, b, c, d)$ would be represented as $△(△ab)(△cd)$.


# Representing trees as text
To communicate trees precisely and concisely, it makes sense to come up with some conventions.
Implementations for some of these can be found in this repo, for instance [here](../implementation/typescript/src/format).

### Human-readable terms
This hardly counts, but for completeness: We may choose to represent expressions directly, using `△` for the node operator, assuming left-associativity of application and using parentheses otherwise.
For instance, the identity program $△ (△ (△ △)) △$ would be `△ (△ (△ △)) △`.

Note: This can represent unreduced expressions as well, e.g. the expression `△ △ △ △` is reducible and reduces to `△`.

### Ternary
We can represent unlabeled binary trees using their _preorder arity encoding_, which yields `0` for bare leaf nodes, `1...` for stems (have one child) and `2...` for forks (have two children).
This corresponds to the $\textbf{num}\\{...\\}$ operator from [Typed Program Analysis without Encodings](https://github.com/barry-jay-personal/typed_tree_calculus/blob/main/typed_program_analysis.pdf) (Barry Jay, PEPM 2025), chapter 3.

For example, the identity program would be `21100`.

Note: This can only represent fully reduced values. While it may seem easy to extend the encoding to cover trees with higher arity, it requires further complexity/definitions to unambiguously represent nodes of arity > 9.

### DAG (directed acyclic graph)
The trees representing large data or programs tend to result in the same subtrees appearing multiple times (e.g. from the same characters appearing several times in some text or from the same function being used in multiple spots).
This suggests making use of sharing both when representing trees in memory, but also when communicating them.

"Trees with sharing" are really directed acyclic graphs.
We draw inspiration from how let-bindings make some expression available to subsequent code via some name, e.g.
```
let k = △ △ in
let △k = △ k in
let sk = △ △k in
let i = sk △ in
let false = k i in
false
```
defines a DAG where sub-DAG `k` is "pointed to" twice by the `false` sub-DAG, once directly and once indirectly via `i`. The corresponding tree contains subtree `k` twice.

The format we use sometimes is analogous to let-bindings, except that it drops all additional syntax. The above example would be:
```
k △ △
△k △ k
sk △ △k
i sk △
false k i
false
```

### Minimalist binary
We can represent any expression (reduced or not) as a binary tree where leaves are `△` and inner nodes are application. Note that this is different from the tree structure we usually consider in tree calculus, in that it makes application explicit!
For example, we usually think of `△ △ △` as a tree with three nodes `fork(leaf,leaf)`. But here, we want to think of it as the tree with five nodes `app(app(△,△),△)`.

We pick a _preorder encoding_ where inner nodes (application) are `0` and leaves (`△`) are `1`. The example above would be `00111`.

The identity program is `001010111`.

Observations:
* Any (sub)string `0...1` with one more `1` than `0`s represents a (sub)expression.
* Expression are reducible iff their encoding contains substring `000`.
* The triage calculus reduction rules are
  ```
  00011ab -> a
  000101abc -> 00ac0bc
  0001001abc1 -> a
  0001001abc01 -> 0b
  0001001abc001 -> 00c
  ```
  where `a`,`b` and `c` are subexpressions.
* This allows writing very minimalistic evaluators, such as the PCRE-based one used by [this demo](https://treecalcul.us/live/?example=portability).