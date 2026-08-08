#!/usr/bin/env bash
# Runs ./reduce.sh over a table of expression / normal form pairs.
cd "$(dirname "$0")" || exit 1

fail=0
check() { # check <expression> <expected normal form> <what it is>
  actual=$(echo "$1" | ./reduce.sh)
  [ "$actual" = "$2" ] && return
  echo "FAIL: $3"
  echo "  $1 -> $actual, expected $2"
  fail=1
}

# Values reduce to themselves.
check '1'                    '1'         'leaf'
check '011'                  '011'       'stem'
check '00111'                '00111'     'fork'

# One check per reduction rule, with leaves for the subexpressions.
check '00011100111'          '1'         'rule 00011ab -> a'
check '000101111'            '0011011'   'rule 000101abc -> 00ac0bc'
check '00010011111'          '1'         'rule 0001001abc1 -> a'
check '0001001111011'        '011'       'rule 0001001abc01 -> 0b'
check '000100111100111'      '00111'     'rule 0001001abc001 -> 00c'

# Programs: identity, and negation of both booleans.
check '0 001010111 1'        '1'         'identity of false'
check '0 001010111 011'      '011'       'identity of true'
check '0 001001011001111 1'  '011'       'not false'
check '0 001001011001111 011' '1'        'not true'

[ $fail = 0 ] && echo 'all tests passed'
exit $fail
