import { test as test_formatters } from "./format/test";
import { test as test_evaluators } from "./evaluator/test";
import { test as test_abs_elimination } from "./abstraction-elimination/test";

test_formatters();
test_evaluators();
test_abs_elimination();
