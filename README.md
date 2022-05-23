# Program-Analysis

Implementing basic program analysis function.

Using LLVM version 12.0.0.

- \[**New** 2022/05/22\]: Now it only support Intra-procedural Reaching Definition Analysis.

## Folder tree

There is a `CMakeLists.txt` outside the `hytProgramAnalysis` folder. Its content(s) should be added to your own llvm project folder, e.g., `/path/to/llvm-project/llvm/lib/Transforms/`

Besides that, the `hytProgramAnalysis` folder should also be added to the same directory as previous `CMakeLists.txt` is, e.g., `/path/to/llvm-project/llvm/lib/Transforms/`. 

The `hytProgramAnalysis` folder consists of the following things.

```bash
$ tree hytProgramAnalysis 
hytProgramAnalysis
├── CMakeLists.txt
├── HytDFA.cpp
└── test.cpp

0 directories, 3 files
```

`HytDFA.cpp` contains the source code of Reaching Definition Analysis.

`test.cpp` is the test version of `HytDFA.cpp`.

Note that, **THIS** `CMakeLists.txt` file contains the build information of this folder, which is **DIFFERENT** from previous one outside `hytProgramAnalysis` folder.

## Intra-procedural Data Flow Analysis

### Reaching Definition Analysis

Source code is in `hytProgramAnalysis/HytDFA.cpp`.

### Liveness Variable Analysis

TODO
