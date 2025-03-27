This directory contains just enough to get off the ground quickly with minimal dependencies or setup.
See [`implementation/`](../implementation/) for proper implementations using various languages and strategies.

`main.js` is a standalone tool for applying tree calculus programs to arguments or converting trees that represent programs or data between various [formats](../conventions/). It only requires [Node.js](https://nodejs.org/en) to be installed.

## Examples

``` bash
# Applying the identity function (specified in various formats) to various data:
$ ./main.js -term '△ (△ (△ △)) △' -string 'hello world'
hello world
$ ./main.js -ternary 21100 -bool true
true
$ ./main.js -ternary 21100 -bool true -term
△ △
$ ./main.js -ternary 21100 -term '△ △' -bool
true

# Loading larger arguments from files:
$ echo 212121201121211002110010202120212011201120212120112121100211001020212021201221000212011222011020112010010212011212011212110021100101021212001211002121202121202120002120102120002010212011202120212000101120212021200010211002120112120112121100211001010200 > /tmp/size.ternary
$ ./main.js -ternary -file /tmp/size.ternary -term '△' -nat
1
$ ./main.js -ternary -file /tmp/size.ternary -term '△ △' -nat
2
$ ./main.js -ternary -file /tmp/size.ternary -string hello -nat
102
$ ./main.js -ternary -file /tmp/size.ternary -file /tmp/size.ternary -nat
252

# The above examples all specify two trees, a function and an argument.
# It doesn't have to be like that:
$ ./main.js -nat 42 -term
△ △ (△ (△ △) (△ △ (△ (△ △) (△ △ (△ (△ △) △)))))
$ ./main.js -term '△ △' -nat 42 1337
42
$ ./main.js -term '△ △' '△ △ △' △
△ △ △
```

## Usage

```
$ ./main.js <param>+
```
where `<param>` is one of
* A flag that modifies how subsequent trees are parsed or printed.
  * `-bool`, `-nat`, `-string` follow the conventions for representing data described [here](../conventions/).
  * `-ternary`, `-term`, `-dag` follow the conventions for representing trees described [here](../conventions/).
  * `-infer` tries to guess which of the above formats are used. This is the default behavior as long as no explicit format has been specified. However, this has potential to be quite confusing, so explicitly passing formats is strongly recommended.
  * `-file` is orthogonal to all other flags in that it does not update the expected format, but merely causes the next tree (and only the next one) to be read from a file rather than directly from the command line.
* A tree that is either program or argument to said program.

In summary, parameters are parsed from left to right. Any trees provided are parsed according to whatever format was last specified. The format specified last overall will be used for printing the result tree. See examples above.