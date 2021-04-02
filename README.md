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

## Getting Started Guide
This repository includes one end-to-end example of how to apply selective profiling to monitor in-production data locality issues as well as one example data locality optimization applied in a targetted manner based on the output of selective profiling.
