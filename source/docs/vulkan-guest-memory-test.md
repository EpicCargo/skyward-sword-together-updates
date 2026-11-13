# Vulkan guest-memory readback

The asset-free Linux fixture `tests/vulkan_guest_memory_test.c` synthesizes Maxwell instructions, translates them through the current engine, compiles the resulting GLSL and checks real Vulkan compute readback. It imports 128 MiB plus 64 KiB on the tested Mesa llvmpipe driver, exceeding its storage-descriptor limit without exposing that heap through a storage descriptor.

All 14 cases passed on November 13 with address/undefined sanitizers and zero Khronos validation warnings. The exact translator, fixture and memory-import header hashes are recorded in `validation/vulkan-guest-memory-readback-20261113.json`. Cases include reads beyond 128 MiB, complete-span read bounds, a zero-size fallback, a real zero-backed fallback, a 128-bit store ending exactly at the heap boundary, and rejected stores crossing or starting beyond the live boundary. These checks create an offscreen Vulkan compute device; game files and a window are not needed.

Prerequisites are Clang, Vulkan headers/loader, shared shaderc, Mesa llvmpipe and `VK_LAYER_KHRONOS_validation`. Restore the pinned engine first. From `source/`, build with installed headers and libraries (add explicit `-I`, `-L` and runtime-library paths for separately verified dependency caches):

```sh
mkdir -p build/vulkan-guest-memory
python3 scripts/resource_runner.py --rss-mib 512 --available-mib 6144 --nice 10 \
  --lock build/developer-build.lock -- clang -O1 -g -Wall -Wextra -std=c11 \
  -D_GNU_SOURCE -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/vulkan_guest_memory_test.c engine/runtime/nvn/maxwell/translate.c \
  -lshaderc_shared -lvulkan -lm -o build/vulkan-guest-memory/check
```

Select the llvmpipe ICD using `VK_DRIVER_FILES` and `VK_ICD_FILENAMES`; set `VK_LAYER_PATH` if validation layers are in a dependency cache. Use `LP_NUM_THREADS=1`. For each mode 0 through 13, run `check --compile MODE OUTPUT.spv` followed by `check --run MODE OUTPUT.spv` under the same resource guard. Each command exits nonzero on failure. Shader compilation and readback run in separate processes to stay within the memory budget. The fixture intentionally fails when the required validation layer is absent or the driver's descriptor limit would require an import exceeding 256 MiB.

The local continuation used the project's guarded CPU runner with a 512 MiB cap and 3 GiB reserve; peak sampled group RSS was 436.9 MiB. The public general build guard retains its 6 GiB reserve. Generated SPIR-V and executables stay in the local build directory.

This replaces the earlier pre-refactor GPU receipt as evidence for the current source. Game rendering and Windows/NVIDIA behavior still require their own validation.
