# DMon Prototype for OSDI 2021 Artifact Evaluation
We present selective profiling, a technique that locates data locality problems with low-enough overhead that is suitable for production use. To achieve low overhead, selective profiling gathers runtime execution information selectively and incrementally. Using selective profiling, we build DMon, a system that can automatically locate data locality problems in production, identify access patterns that hurt locality, and repair such patterns using targeted optimizations.

Please cite the following paper if you use the selective profiling technique for data locality optimizations:

```
@inproceedings{khan2021dmon,
  author = {Khan, Tanvir Ahmed and Neal, Ian and Pokam, Gilles and Mozafari, Barzan and Kasikci, Baris},
  title = {DMon: Efficient Detection and Correction of Data Locality Problems using Selective Profiling},
  booktitle = {Proceedings (to appear) of the 15th USENIX Symposium on Operating Systems Design and Implementation (OSDI)},
  publisher = {{USENIX} Association},
  series = {OSDI 2021},
  year = {2021},
  month = jul,
}
```

## Required System Configurations and Dependencies
- Linux running on an Intel Processor (perferrably Skylake)
- Linux perf
- [pmu-tools](https://github.com/andikleen/pmu-tools) that implements [Intel's Top-Down Microarchitecure Analysis Method](https://ieeexplore.ieee.org/document/6844459).
- LLVM

## Getting Started Guide
This repository includes one end-to-end example of how to apply selective profiling to monitor in-production data locality issues as well as one example data locality optimization applied in a targetted manner based on the output of selective profiling.

## Example of How to Apply Selective Profiling
The folder `test-case` contains the example of how to apply selective profiling for identifying data locality issues in program executions. The folder contains one example program `stride_benchmark.cpp` (from [DynamoRIO](https://github.com/DynamoRIO/dynamorio/blob/master/clients/drcachesim/tests/stride_benchmark.cpp) code base) as well as a script to actually compile the program with debug symbol, getting binary instruction to C/C++ source code mapping extraction, striping the binary to reduce in-production overhead, running the program, monitoring the program execution at various levels/layers of TMAM, triggering one TMAM layer to the next layer based on program runtime characteristics, collection of cache miss samples, and finally finding program locations that suffer from poor data locality.

## Example of How to Apply Locality Optimizations in a Targetted Manner
The folder `llvm-passes` contains two llvm-pass source codes to apply data prefetching. The key difference among them are one (`prefetch` from the official LLVM code repository) applies the optimization based on pure static heuristics while the other (`selective-prefetch`) applies the same optimization selectively only to a subset of program locations. For example,

```
$ diff prefetch/Prefetch.cpp selective-prefetch/Prefetch.cpp
26a27
> #include "llvm/IR/DebugInfoMetadata.h"
29a31,36
> #include <string>
> #include <set>
> #include <fstream>
> 
> std::set<std::string> selected_instructions;
> 
35a43,44
> static cl::opt<std::string>SelectedInstructionFile("selected-inst-file",cl::desc("file with list of instructions (file name, line number pair)"));
> 
66a76,82
>         if (!SelectedInstructionFile.empty()) {
>           std::ifstream InsFile(SelectedInstructionFile, std::ios::in);
>           std::string InsName;
>           while (std::getline(InsFile, InsName)) {
>             selected_instructions.insert(InsName);
>           }
>         }
236a253,264
>       if (selected_instructions.size() > 0){
>         if(DILocation *Loc = I.getDebugLoc()) {
>           StringRef file_name = Loc->getFilename();
>           uint64_t line_number = Loc->getLine();
>           std::string tmp = file_name.str() + " " + std::to_string(line_number);
>           if (!selected_instructions.count(tmp)) {
>             continue;
>           }
>         } else {
>           continue;
>         }
>       }
```

Based on the profile information collected via selective profiling, DMon applies static locality optimizations in a targetted manner.
