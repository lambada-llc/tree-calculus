This is a collection of tree calculus evaluators written in TypeScript. 

Implementation strategies vary along several dimensions:
* Evaluation:
  * Eager, branch-first. Canonical example: `eager-value-adt.ts`
  * Lazy, root-first. Canonical example: `lazy-value-adt.ts`
* How to represent programs/values:
  * Explicitly represented leafs, stems and forks. Canonical example: `eager-value-adt.ts`
  * Explicit tree of applications. Canonical example: `eager-node-app.ts`
  * Functions in the host language. Canonical example: `eager-func.ts`
* How to represent applications:
  * Implicitly, as a function call in the host language. Tends to imply deep call stacks and eager evaluation if host language is eager. Canonical example: `eager-value-adt.ts`
  * Implicitly, as non-binary tree nodes. Canonical example: `eager-stacks.ts`
  * Explicitly. Canonical example: `lazy-value-adt.ts`
* How to manage memory:
  * Implicitly via host language (here: JavaScript GC). Canonical example: `{eager,lazy}-value-adt.ts`
  * Explicitly. Canonical example: `eager-value-mem.ts`

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
npm start
```
