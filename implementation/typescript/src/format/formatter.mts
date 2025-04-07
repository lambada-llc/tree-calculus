import { Evaluator } from "../common.mjs";

export interface Formatter {
  to: <TTree>(e: Evaluator<TTree>, x: TTree) => string;
  of: <TTree>(e: Evaluator<TTree>, s: string) => TTree;
}
