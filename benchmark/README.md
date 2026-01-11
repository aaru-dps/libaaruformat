# Aaru Format Compression Benchmark Tool

This tool benchmarks different compression algorithms on Aaru format images without modifying the library itself.

## Purpose

The benchmark tool helps determine the most effective compression algorithm for specific image types by:
- Testing multiple compression algorithms (LZMA, Bzip3, Zstd)
- Measuring compression ratios and processing times
- Providing detailed performance metrics

## Features

- **Non-invasive**: Does not modify library code - operates on internal structures directly
- **Comprehensive**: Tests all major compression algorithms
- **Progressive**: Shows real-time progress bars for each operation
- **Isolated**: Compression algorithms are confined to the benchmark tool only
- **Detailed results**: Provides comparison tables with sizes, ratios, and timing

## Usage

```bash
aarubenchmark <input.aaruformat>
```

### Example

```bash
aarubenchmark myimage.aaruformat
```

This will:
1. Open and analyze the input image
2. For each compression algorithm:
   - Decompress all data blocks
   - Recompress with the test algorithm
   - Write a new image file (e.g., `myimage.aaruformat.LZMA.aaruformat`)
   - Display progress and timing
3. Show a summary table comparing all algorithms

## Output

The tool creates test images for each algorithm:
- `input.aaruformat.LZMA.aaruformat`
- `input.aaruformat.Bzip3.aaruformat` (if bzip3 is available)
- `input.aaruformat.Zstd.aaruformat` (if zstd is available)

And displays a comparison table:
```
Algorithm  Uncompressed    Compressed      Ratio        Time (s)
----------  ---------------  ---------------  ------------  ----------
LZMA       1.50 GB         450.23 MB        30.01%       45.23
Bzip3      1.50 GB         425.67 MB        28.38%       52.17
Zstd       1.50 GB         475.89 MB        31.73%       12.45

Best compression: Bzip3
Fastest: Zstd
```

## Requirements

### Mandatory
- libaaruformat (automatically linked)
- LZMA support (built into library)

### Optional (Automatic)
- **bzip3**: Automatically downloaded and built from GitHub
  - No manual installation needed
  - CMake fetches and builds it automatically

### Optional (Manual)
- **zstd**: For Zstd compression testing
  - Install: `brew install zstd` (macOS) or `apt install libzstd-dev` (Linux)

Algorithms without available libraries will be skipped automatically.

## Building

The benchmark tool is built automatically when building the main project:

```bash
cd libaaruformat
mkdir build && cd build
cmake ..
make
```

The compiled binary will be in `build/bin/aarubenchmark`.

## Implementation Details

### How It Works

1. **Opens image manually**: Reads header and index using structs directly
2. **Iterates blocks**: Uses index entries to locate all data blocks
3. **Decompresses**: Uses library's LZMA decoder to decompress existing data
4. **Recompresses**: Applies test algorithm with optimal settings
5. **Writes output**: Creates proper header and index for the new image
6. **Measures**: Records timing and file sizes for comparison

### Isolation

The benchmark tool is completely isolated:
- No library code is modified
- Compression algorithms are in `benchmark/` directory only
- Only the main CMakeLists.txt is updated to include the benchmark subdirectory
- The library continues to work exactly as before

### Memory Safety

- All allocations are checked
- Proper cleanup on errors
- No memory leaks (validated with valgrind)

## Limitations

- Only tests data block compression (not metadata or other blocks)
- Requires sufficient disk space for test output files
- Only works with Aaru format version 2 images

## Contributing

To add a new compression algorithm:

1. Add the algorithm enum to `benchmark.h`
2. Implement compression function in `compression.c`
3. Update `get_compression_type()` to return a unique identifier
4. Add library dependency to `CMakeLists.txt`
5. Update this README

## License

Same as libaaruformat - LGPL 2.1 or later.

