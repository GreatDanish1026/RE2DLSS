// Vtable slot numbers from the SDK's C declarations (so no hand-counted indices).
#define CINTERFACE
#define COBJMACROS
#include <stddef.h>
#include <d3d12.h>
#include <dxgi1_2.h>

#define SLOT(vtbl, m) (int)(offsetof(vtbl, m) / sizeof(void*))

void ProbeSlots(int* s) {
    s[0] = SLOT(ID3D12DeviceVtbl, CreateCommandQueue);
    s[1] = SLOT(ID3D12DeviceVtbl, CreateGraphicsPipelineState);
    s[2] = SLOT(ID3D12DeviceVtbl, CreateComputePipelineState);
    s[3] = SLOT(ID3D12DeviceVtbl, CreateCommandList);
    s[4] = SLOT(ID3D12DeviceVtbl, CreateRootSignature);
    s[5] = SLOT(ID3D12DeviceVtbl, CreateShaderResourceView);
    s[6] = SLOT(ID3D12DeviceVtbl, CopyDescriptors);
    s[7] = SLOT(ID3D12DeviceVtbl, CopyDescriptorsSimple);
    s[8] = SLOT(ID3D12Device1Vtbl, CreatePipelineLibrary);
    s[9] = SLOT(ID3D12Device2Vtbl, CreatePipelineState);
    s[10] = SLOT(ID3D12Device4Vtbl, CreateCommandList1);
    s[11] = SLOT(ID3D12GraphicsCommandListVtbl, DrawInstanced);
    s[12] = SLOT(ID3D12GraphicsCommandListVtbl, DrawIndexedInstanced);
    s[13] = SLOT(ID3D12GraphicsCommandListVtbl, Dispatch);
    s[14] = SLOT(ID3D12GraphicsCommandListVtbl, SetPipelineState);
    s[15] = SLOT(ID3D12GraphicsCommandListVtbl, ExecuteIndirect);
    s[16] = SLOT(ID3D12CommandQueueVtbl, ExecuteCommandLists);
    s[17] = SLOT(IDXGIFactory2Vtbl, CreateSwapChainForHwnd);
    s[18] = SLOT(ID3D12DeviceVtbl, CreateUnorderedAccessView);
    s[19] = SLOT(ID3D12DeviceVtbl, CreateRenderTargetView);
    s[20] = SLOT(ID3D12PipelineLibraryVtbl, LoadGraphicsPipeline);
    s[21] = SLOT(ID3D12PipelineLibraryVtbl, LoadComputePipeline);
    s[22] = SLOT(ID3D12PipelineLibrary1Vtbl, LoadPipeline);
}
