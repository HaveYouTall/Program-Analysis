# Program-Analysis

Implementing basic program analysis function.

Using LLVM version 12.0.0.

- \[**New** 2022/07/10\]: Now it supports Intra-procedural **Available Expressions Analysis**.

- \[**New** 2022/06/10\]: Now it supports Intra-procedural **Live Variables Analysis**.

- \[**New** 2022/05/22\]: Now it only supports Intra-procedural **Reaching Definition Analysis**.

## Folder tree

There is a `CMakeLists.txt` outside the `hytProgramAnalysis` folder. Its content(s) should be added to your own llvm project folder, e.g., `/path/to/llvm-project/llvm/lib/Transforms/`

Besides that, the `hytProgramAnalysis` folder should also be added to the same directory as previous `CMakeLists.txt` is, e.g., `/path/to/llvm-project/llvm/lib/Transforms/`. 

The `hytProgramAnalysis` folder consists of the following things.

```bash
$ tree hytProgramAnalysis 
hytProgramAnalysis
├── CMakeLists.txt
├── HytAEA.cpp
├── HytDFA.cpp
├── HytLVA.cpp
└── test.cpp

0 directories, 5 files
```

`HytLVA.cpp` contains the source code of **Live Variables Analysis**. 

`HytDFA.cpp` contains the source code of **Reaching Definition Analysis**.

`HytAEA.cpp` contains the source code of **Available Expressions Analysis**

`test.cpp` is the test version of `HytDFA.cpp`.

Note that, **THIS** `CMakeLists.txt` file contains the build information of this folder, which is **DIFFERENT** from previous one outside `hytProgramAnalysis` folder.

## Intra-procedural Data Flow Analysis

### Reaching Definition Analysis

Source code is in `hytProgramAnalysis/HytDFA.cpp`.

### Live Variables Analysis

Source code is in `hytProgramAnalysis/HytLVA.cpp`.

### Available Expressions Analysis

Source code is in `hytProgramAnalysis/HytAEA.cpp`.

### Pointer Analysis (Context insensitive)

Working...

### More Analysis

TODO...