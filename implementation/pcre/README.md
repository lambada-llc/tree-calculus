# Tree Calculus as Five Regular Expressions

`reduce.sh` is a complete evaluator whose entire logic is five PCRE
substitutions applied until none of them matches:

```
s/00011(1|0(?1)(?1))(1|0(?2)(?2))/$1/g;
s/000101(1|0(?1)(?1))(1|0(?2)(?2))(1|0(?3)(?3))/00$1${3}0$2$3/g;
s/0001001(1|0(?1)(?1))(1|0(?2)(?2))(1|0(?3)(?3))1/$1/g;
s/0001001(1|0(?1)(?1))(1|0(?2)(?2))(1|0(?3)(?3))01/0$2/g;
s/0001001(1|0(?1)(?1))(1|0(?2)(?2))(1|0(?3)(?3))001/00$3/g;
```

Expressions are read and written in the [minimalist binary
encoding](../../conventions/#minimalist-binary): `1` is a leaf and `0AB` is the
application of `A` to `B`. In that encoding the [reduction
rules](../../reduction-rules/) are

```
00011ab       -> a
000101abc     -> 00ac0bc
0001001abc1   -> a
0001001abc01  -> 0b
0001001abc001 -> 00c
```

and `(1|0(?k)(?k))` — a group that recurses into itself, `k` being its own index
in the pattern — is the regular expression matching a subexpression, which is
what lets the substitutions above stand in for `a`, `b` and `c`.

Requires `perl`. See [this demo](https://treecalcul.us/live/?example=portability)
for the same rules running in a browser.

## Usage

```sh
$ echo '0 001010111 011' | ./reduce.sh   # identity applied to true
011
$ ./test.sh
all tests passed
```
