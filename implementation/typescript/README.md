This is a collection of tree calculus evaluators written in TypeScript. 

Implementation strategies vary along several dimensions:
* Evaluation:
  * Eager, branch-first. Canonical example: `eager-value-adt.mts`
  * Lazy, root-first. Canonical example: `lazy-value-adt.mts`
* How to represent programs/values:
  * Explicitly represented leafs, stems and forks. Canonical example: `eager-value-adt.mts`
  * Explicit tree of applications. Canonical example: `eager-node-app.mts`
  * Functions in the host language. Canonical example: `eager-func.mts`
* How to represent applications:
  * Implicitly, as a function call in the host language. Tends to imply deep call stacks and eager evaluation if host language is eager. Canonical example: `eager-value-adt.mts`
  * Implicitly, as non-binary tree nodes. Canonical example: `eager-stacks.mts`
  * Explicitly. Canonical example: `lazy-value-adt.mts`
* How to manage memory:
  * Implicitly via host language (here: JavaScript GC). Canonical example: `{eager,lazy}-value-adt.mts`
  * Explicitly. Canonical example: `eager-value-memory.mts`

A few files pair a strategy with a variant of itself, suffixed `-opt` (tuned)
or `-alt` (an alternative encoding).
Those reduce identically to the file they are named after — same rules, same
order, same number of steps — and differ only in how that reduction is carried
out on the host: what gets allocated, and what the host is asked to do per step.

## Getting Started

### Prerequisites
* Install [Node.js](https://nodejs.org/en/download)
* Run `npm install` here to install build dependencies

### Build
```
npm run build
```
or
```
npm run build -- --watch
```

### Run tests and small benchmarks
```
npm test
```

### Run commander
```
npm start
```
