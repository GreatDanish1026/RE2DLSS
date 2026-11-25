# RE2DLSS 2.0 phase 0: can a REFramework plugin replace ReShade? (2026-11-25)

Probe: `probe.cpp` (+ `vtidx.c` for vtable slots from the SDK's C declarations), built by `build_probe.ps1`
into `reframework\plugins\RE2Probe.dll`. Observes only. Logs `run1..4-*.log`.

**Verdict: GO.**

| Question | Answer |
|---|---|
| Loaded early enough? | Plugin DllMain at 0.2-1.0 s; game's `D3D12CreateDevice` at 1.0-3.7 s; swapchain 2-10 s; REFramework's `reframework_plugin_initialize` only at 8.7-18.4 s. So hooks must go in from DllMain, not from plugin initialize. |
| How to catch the device | re2.exe has no d3d12 import: it `GetProcAddress`es `D3D12CreateDevice` (also `D3D12SerializeVersionedRootSignature`, `D3D12GetDebugInterface`). Patching re2.exe's `GetProcAddress` IAT slot works. dxgi is a static import (`CreateDXGIFactory2` used). |
| Do we see every PSO? | Yes, 0 unknown binds once all creation paths are hooked: `CreateComputePipelineState` (~1380), `ID3D12Device2::CreatePipelineState` stream PSOs (~4100: all graphics), `ID3D12PipelineLibrary(1)::LoadPipeline` (484 when the 50 MB library is valid; misses fall back to CreatePipelineState). `CreateGraphicsPipelineState` only 10. Stream desc parsing needed for PS/CS bytecode. |
| Same shader ids? | Yes: TAA ps89b84994, bloom psdd975721, tonemap ps0936c4dc / psd127432d, output ps6769b8ed, CAS csa2614017, velocity ps5ad41227, RT upscale cse120127f all created and bound ~1/frame (no ReShade). |
| With ReShade present | Game gets ReShade's wrapper objects (vtables in ReShade's dxgi.dll): we sit ABOVE ReShade/RenoDX and see the game's original bytecode. One list class only. |
| Without ReShade | Native D3D12Core objects; THREE command list vtables (hook each, originals per vtable). Swapchain vtable in dxgi.dll. |
| Call rates (no ReShade, 4K, RT) | per frame: ~125 draws, 50-250 indexed, 135-470 dispatch, 130-370 SetPipelineState, 80-590 ExecuteIndirect, 9-12 lists executed, 50-250 views created, ~600-1000 CopyDescriptorsSimple (CopyDescriptors 0), 3035 root sigs total. Light. |

Design consequences for 2.0: ExecuteIndirect is the main geometry path (indirect draw handling matters);
views are created per frame and copied with CopyDescriptorsSimple (descriptor tracking must be cheap);
with ReShade present we're a layer above it, so our own D3D12 calls and RenoDX's per-draw state interplay
differ from 1.x (RenoDX's state is applied below us, after our redirects).
