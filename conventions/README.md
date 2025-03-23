[Reduction rules](../reduction-rules) imply how to represent _logic_, for instance $△ (△ (△ △)) △$ is an identity program.
However, how to represent traditional _data_ such as number or text, is up to us.

## Extensional
Whatever works in λ-calculus and combinatory logic also works in tree calculus.
[Church encoding](https://en.wikipedia.org/wiki/Church_encoding) or [Scott encoding](https://en.wikipedia.org/wiki/Mogensen%E2%80%93Scott_encoding) produce certain terms and their values can be deduced by observing how they _behave_. For instance, it is typical to define _true_ as $λa.λb.a$ (in tree calculus: $△ △$) and _false_ as $λa.λb.b$ (in tree calculus: $△ △ (△ (△ (△ △)) △)$). Booleans encoded this way can then be distinguished by applying them to two arguments, effectively a "then" branch and "else" branch.

Note that extensional encodings like this tend to result in one and the same value being representable by infinitely many (internally different) terms. See [this demo](https://treecalcul.us/live/?example=bootstrap-basics) for a demonstration of this fact using Church encodings of the number 3.

## Intensional
Since tree calculus is intensional, programs can directly observe the tree structure of values (including programs).
This allows us to use trees for represent values directly.

For example:
* Booleans: It seems reasonable to represent _false_ and _true_ using $△$ and $△ △$, respectively. These are the two smallest trees and can be distinguished with a single triage reduction.
* Small natural numbers can be represented as $△^n△$, meaning $0$ is just a node $△$ and $n+1$ is a stem $△ n$. Visualized, these "trees" are just chains with a length of $n$ edges.
* Lists can be defined in the usual algebraic style, with empty lists represented by a node $△$ and non-empty lists by a fork $△\ hd\ tl$.
* Large natural numbers can be represented by their binary encoding, as a list of booleans.
* Text can be represented as a list of natural numbers that are for instance the code of each Unicode character.
* Option types can use a leaf for no value and a stem for some value.
* Fixed-size tuples can be represented by connecting the tuple's values using forks, yielding logarithmic time complexity for primitive operations on a tuple. For instance, tuple $(a, b, c, d)$ would be represented as $△(△ab)(△cd)$.
