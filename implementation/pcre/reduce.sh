#!/usr/bin/env bash
# A tree calculus evaluator that is nothing but five regular expressions.
#
# Reads one expression in minimalist binary encoding from stdin (whitespace
# ignored), writes its normal form to stdout in the same encoding:
# `1` is a leaf and `0AB` is the application of A to B, see ../../conventions/.
#
# `(1|0(?k)(?k))` matches a subexpression, where k is that group's own index in
# the pattern -- the recursion is what lets a regular expression see tree shape.
# The five substitutions are the reduction rules; applying them until none
# matches is the whole evaluator.
exec perl -0777 -ne '
  s/\s+//g;
  1 while
    s/00011(1|0(?1)(?1))(1|0(?2)(?2))/$1/g ||
    s/000101(1|0(?1)(?1))(1|0(?2)(?2))(1|0(?3)(?3))/00$1${3}0$2$3/g ||
    s/0001001(1|0(?1)(?1))(1|0(?2)(?2))(1|0(?3)(?3))1/$1/g ||
    s/0001001(1|0(?1)(?1))(1|0(?2)(?2))(1|0(?3)(?3))01/0$2/g ||
    s/0001001(1|0(?1)(?1))(1|0(?2)(?2))(1|0(?3)(?3))001/00$3/g;
  print "$_\n";
'
