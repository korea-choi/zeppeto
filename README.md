# zeppeto: LSM-tree dedicated to skewed distribution like zipfian and pareto.
  * zeppeto use extra-cache to in-place update and read hot-keys. It reduced compaction entries by 40%, and increased write throughput up to 63%.
  * Traditional LSM-tree KV store doesn't update kv pairs in in-place update manner. Memtable doesn't support it due to concurrent memtable writes, and also SST table out-place updates through compaction.
  * However in skewed workload like zipfian and pretto distribution, hot kv pairs generates huge obsolete garbages that increase compaction overhead and decrease throughput.
  * It is built on earlier work on [LevelDB](https://github.com/google/leveldb) by Sanjay Ghemawat (sanjay@google.com) and Jeff Dean (jeff@google.com)

# Documentation
 * Find out more description and experiment results here!

# Limitations
  * Do not support range-query, yet.
  * Throughput spike ocuurs due to hashmap resizing.

# Build for POSIX

```bash
git clone --recurse-submodules https://github.com/korea-choi/ZEPPETO.git
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build .
```

# Benchmarks: YCSB workload A
Download [YCSB-cpp]():
```bash
git clone https://github.com/ls4154/YCSB-cpp.git
```
Modify config section in `Makefile`:
```
EXTRA_CXXFLAGS ?= -I/example/leveldb/include
EXTRA_LDFLAGS ?= -L/example/leveldb/build -lsnappy

BIND_LEVELDB ?= 1
```
Run workload A:
```
./ycsb -load -run -db leveldb -P workloads/workloada -P leveldb/leveldb.properties -s
```

# Related Studies
- [TRIAD (ATC '17)](https://www.usenix.org/conference/atc17/technical-sessions/presentation/balmau): Creating Synergies Between Memory, Disk and Log in Log Structured Key-Value Stores
- [HotKey-LSM (BigData '20)](https://ieeexplore.ieee.org/abstract/document/9377736): A Hotness-Aware LSM-Tree for Big Data Storage
