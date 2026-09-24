# Ideas for DepVeC

## DFG-Extract
* Emit
    * CFG
    * DFG-edges inside CFG
    * A syntactic language for (annotated) DFGs that can be put next to the LLVM .s file

## DFG-Annotator
* Input
    * CFG + DFG
* Interactive
    * Graphically represent the CFG+DFG
* Emit
    * Annotated DFG

## Verification Condition Checker
* Input
    * Annotated DFG
* Output
    * Failing + Succeeding VCs

# Upgrade Annotator
* Make DFG-Annotate update succeeding conditions in real-time
