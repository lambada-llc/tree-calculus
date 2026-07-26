import { test as test_formatters } from "./format/test.mjs";
import { test as test_evaluators } from "./evaluator/test.mjs";
import { test as test_abs_elimination } from "./abstraction-elimination/test.mjs";
import { test as test_modules } from "./module/test.mjs";

test_formatters();
test_evaluators();
test_abs_elimination();
test_modules();
