# Bound-handle texture sampling

Patch48 supports Vulkan GLSL fragment `TEX.B` for 2D, non-array, non-depth, implicit-LOD samples. The handle must come directly from an unconditional 32-bit load of the hardware c2 bound-handle table. Unknown intervening write sets, clobbered handles, live control flow and other forms fail translation. Unreachable self-branch padding after unconditional EXIT is permitted. Metal retains its prior rejection.

The translator compares the handle against the 32 stage-bound entries and samples the corresponding statically declared sampler. It evaluates derivatives before selection and uses `textureGrad`, preserving mip selection when neighboring fragments use different textures. It requires no descriptor-indexing extension. Zero or unmatched handles return zero; duplicate handles select the first identical entry. Existing c2 snapshots isolate earlier draws from later rebind/unbind operations. Vulkan checks sampler/image descriptor counts against the adapter's limits before compilation.

After restoring the source, the CPU check uses the actual translator:

```sh
clang -O1 -g -std=c11 -D_GNU_SOURCE -fsanitize=address,undefined \
  -fno-omit-frame-pointer engine/tools/test_bindless_texture.c -lm -o /tmp/bindless-check
/tmp/bindless-check
```

It covers 9 supported cases and 25 rejection controls, including source/destination overlap, stale handle provenance, texture shapes and terminal padding. The exact patch47 translator reproduces the 9 missing-support cases. The separate texture-c2 check retains 35 boundary and snapshot cases.

The [GPU workflow](../../.github/workflows/bindless-validation.yml) links `tests/bindless_gpu_test.c` to the actual translator and shaderc. It needs Vulkan, Mesa software rendering and the Khronos validation layer. It compiles only synthetic shaders, then runs each mip case in a separate process. Address, undefined-behavior and leak sanitizers remain enabled. The hosted pipeline compiler has a 1024 MiB owned-group cap and 6144 MiB free-memory reserve; compilation and local small fixtures retain 512 MiB caps. No game files or window are used.

The GPU fixture checks all 32 handle entries with neighboring pixels selecting different handles, 3 mip levels, final-slot rebinding/unbinding, duplicate handles and binding snapshots. Twelve draws check all 768 output pixels exactly, including sampled float bits, with zero validation warnings/errors. One variant includes terminal padding.

The private offline compiler audit covers 2096 paired vertex/fragment programs (4192 stages). All generated Vulkan GLSL stages compile to SPIR-V with paired varying masks and otherwise default keys. This does not establish actual pipeline linkage, render-state compatibility or gameplay correctness. The 72 previously unsupported fragment variants still reject in Metal. Only aggregate counts and source hashes are published; game shader bytes and generated game shaders stay private.

Controllable Link, opening visuals on the new runtime, native co-op interaction and independent full clients remain separate acceptance checks.
