# Ideas for DepVeC

## DFG-Extract
* Emit
    * CFG
    * DFG-edges inside CFG
    * A syntactic language for (annotated) DFGs that can be put next to the LLVM .s file

## DFG-Annotator
* Input
    * CFG + DFG
* Assertion Language
    * Variables: %-vars from LLVM + Memory vars per alloc-site
* Interactive
    * Graphically represent the CFG+DFG
* Emit
    * Annotated DFG


## Verification Condition Checker
* Input
    * Annotated DFG
* Output
    * Failing + Succeeding VCs

### Thoughts on branches and phi nodes
For branches, we could:
* [x] make two nodes for conditional branches (true and false node).
* leave it be one node, but generate true_precond and false_precond.
For phi nodes, we could:
* merge them into the following using instructions.
* [x] leave them as a concrete instruction (I think this is fine, because they can move along into the branches during interference detection, and also down into their uses. If used twice, though, only as far as the first will go down.) 

# Upgrade Annotator
* Make DFG-Annotate update succeeding conditions in real-time
