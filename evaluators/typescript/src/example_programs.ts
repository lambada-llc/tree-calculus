// Check out https://treecalcul.us/live/?example=demo-serialize-anything to
// interactively convert arbitrary programs into various formats.
// For efficient dag representations, copy the output of the "DAG (out)" tab
// in any output preview section at https://treecalcul.us/live/.

// From "Typed Program Analysis without Encodings" (Jay, PEPM 2025).
export const equal_ternary =
  `212121202120112110102121200212002120112002120
   112002121200212002120102120021200212120021200
   212010211010212010211010202120102110102020211
   010202120112110102121200212002120112002120112
   002121200212002120102120021200212120021200212
   010211010212010211010202120102110102020211010
   202120112220221020202110102020202110102121200
   212002120112002120112012021101021201121101021
   201021101020202020202110102120112011201220211
   010202021101021212002120021201120021201120021
   201120112002120112011200212011201120112002120
   112011201120021212002120021201021200212002120
   112002120112002120112011200212011201120021201
   120112010212120021200212011200212011200212011
   201021201121101021201021101020202110102021201
   120102121200212002120112002120112002120112010
   212011211010212010211010202021101020202020202
   021101010211010`.split(/\s/).join('');

export const succ_dag = `0 t t
1 t 0
2 t 1
3 2 t
4 t 3
5 t 4
6 5 3
7 t 6
8 t 7
9 0 8
10 t 9
11 t 10
12 11 0
13 t 12
14 1 t
15 t 14
16 15 t
17 t 16
18 0 17
19 t 18
20 0 1
21 0 0
22 t 21
23 t 22
24 0 23
25 t 20
26 25 24
27 t 26
28 27 t
29 t 28
30 t 29
31 0 30
32 t 31
33 t 32
34 33 0
35 t 19
36 35 34
37 0 36
38 t 37
39 t 13
40 0 39
41 t 40
42 t 41
43 42 0
44 t 38
45 44 43
46 0 45
47 39 46
47`;