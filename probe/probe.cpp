// RE2Probe.dll: REFramework plugin, RE2DLSS 2.0 phase 0 (no ReShade). Answers:
//  1. Is a plugin DLL loaded before the game creates its D3D12 device, PSOs and swapchain?
//  2. Can we hook those from here (game's GetProcAddress -> D3D12CreateDevice, then vtables)?
//  3. Do all PSOs the game binds go through our creation hooks (no unknown binds)?
//  4. Do we see the game's original shader ids (TAA etc.) with ReShade + RenoDX present?
//  5. Draw/dispatch call rates the hook layer has to handle.
// Log: RE2Probe.log next to re2.exe. Nothing is changed in the game's calls.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_2.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

extern "C" void ProbeSlots(int* s);

namespace {

enum Slot {
    kCreateQueue, kCreateGfxPso, kCreateCsPso, kCreateList, kCreateRootSig, kCreateSrv, kCopyDescs, kCopyDescsSimple,
    kCreatePsoLib, kCreatePsoStream, kCreateList1, kDrawInst, kDrawIndexed, kDispatch, kSetPso, kExecIndirect,
    kExecLists, kCreateSwapHwnd, kCreateUav, kCreateRtv, kLibLoadGfx, kLibLoadCs, kLibLoadStream, kSlotCount
};
int g_slot[kSlotCount];

// ---------------------------------------------------------------- log
std::mutex g_logMutex;
FILE* g_log = nullptr;
ULONGLONG g_t0 = 0;  // process creation (100 ns)

double Ms() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    const ULONGLONG now = (ULONGLONG(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return double(now - g_t0) / 10000.0;
}

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_log) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(g_log, "[%9.1f ms, thread %5lu] %s\n", Ms(), GetCurrentThreadId(), buf);
    fflush(g_log);
}

std::string ModuleOf(const void* p) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(p), &m))
        return "?";
    char path[MAX_PATH] = {};
    GetModuleFileNameA(m, path, MAX_PATH);
    const char* name = strrchr(path, '\\');
    return name ? name + 1 : path;
}

std::string Loaded() {
    std::string s;
    for (const char* m : {"d3d12.dll", "D3D12Core.dll", "dxgi.dll", "sl.interposer.dll", "RE2DLSS.addon64", "dinput8.dll"}) {
        HMODULE h = GetModuleHandleA(m);
        s += std::string(m) + (h ? "=" + ModuleOf(h) + " " : "=no ");
    }
    return s;
}

// ---------------------------------------------------------------- vtable patching
void** g_devVt = nullptr;
void** g_listVt = nullptr;
void** g_queueVt = nullptr;
void** g_factoryVt = nullptr;
void* g_orig[kSlotCount] = {};

void Patch(void** vt, Slot s, void* hook) {
    if (g_orig[s]) return;
    DWORD old;
    void** slot = vt + g_slot[s];
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        Log("patch slot %d failed (%lu)", g_slot[s], GetLastError());
        return;
    }
    g_orig[s] = *slot;
    *slot = hook;
    VirtualProtect(slot, sizeof(void*), old, &old);
}

template <class F>
F Orig(Slot s) {
    return reinterpret_cast<F>(g_orig[s]);
}

// ---------------------------------------------------------------- counters
std::atomic<uint64_t> g_draws{0}, g_drawsIndexed{0}, g_dispatches{0}, g_setPso{0}, g_indirect{0}, g_execLists{0}, g_lists{0};
std::atomic<uint64_t> g_gfxPsos{0}, g_csPsos{0}, g_streamPsos{0}, g_psoLibs{0}, g_rootSigs{0};
std::atomic<uint64_t> g_srvs{0}, g_uavs{0}, g_rtvs{0}, g_copyDescs{0}, g_copyDescsSimple{0};
std::atomic<uint64_t> g_unknownBinds{0};
std::atomic<uint32_t> g_presents{0};
std::atomic<bool> g_firstPso{false}, g_firstDraw{false}, g_firstExec{false};

std::shared_mutex g_psoMutex;
std::unordered_map<void*, uint32_t> g_psoIds;  // PSO -> shader id (ps or cs; 0 = no bytecode seen)
std::unordered_set<void*> g_unknownPsos;

uint32_t Fnv(const void* data, size_t n) {  // same as RE2DLSS main.cpp
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

struct Watch {
    uint32_t id;
    const char* name;
    std::atomic<uint64_t> created{0}, bound{0};
};
Watch g_watch[] = {
    {0x89b84994, "TAA ps89b84994"},       {0xdd975721, "bloom psdd975721"},  {0x0936c4dc, "tonemap ps0936c4dc"},
    {0xd127432d, "tonemap psd127432d"},   {0x6769b8ed, "output ps6769b8ed"}, {0xa2614017, "CAS csa2614017"},
    {0x5ad41227, "velocity ps5ad41227"},  {0xe120127f, "RT upscale cse120127f"},
};

void RecordPso(void* pso, uint32_t id, const char* kind) {
    {
        std::unique_lock<std::shared_mutex> lock(g_psoMutex);
        g_psoIds[pso] = id;
    }
    if (!g_firstPso.exchange(true)) Log("first PSO created by the game (%s, id %08x)", kind, id);
    for (Watch& w : g_watch)
        if (w.id == id && w.created++ == 0) Log("created %s (PSO %p)", w.name, pso);
}

// ---------------------------------------------------------------- device hooks
HRESULT STDMETHODCALLTYPE HkCreateGfxPso(ID3D12Device* d, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** out) {
    const HRESULT hr = Orig<decltype(&HkCreateGfxPso)>(kCreateGfxPso)(d, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ++g_gfxPsos;
        const uint32_t id = desc->PS.pShaderBytecode ? Fnv(desc->PS.pShaderBytecode, desc->PS.BytecodeLength) : 0;
        RecordPso(*out, id, "graphics");
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HkCreateCsPso(ID3D12Device* d, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** out) {
    const HRESULT hr = Orig<decltype(&HkCreateCsPso)>(kCreateCsPso)(d, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ++g_csPsos;
        const uint32_t id = desc->CS.pShaderBytecode ? Fnv(desc->CS.pShaderBytecode, desc->CS.BytecodeLength) : 0;
        RecordPso(*out, id, "compute");
    }
    return hr;
}

// Pipeline state streams: each subobject is {type, data} aligned to a pointer (as d3dx12.h lays them out).
template <class T>
struct alignas(void*) Sub {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
    T data;
};

size_t SubSize(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t) {
    switch (t) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: return sizeof(Sub<ID3D12RootSignature*>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: return sizeof(Sub<D3D12_SHADER_BYTECODE>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: return sizeof(Sub<D3D12_STREAM_OUTPUT_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: return sizeof(Sub<D3D12_BLEND_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK: return sizeof(Sub<UINT>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: return sizeof(Sub<D3D12_RASTERIZER_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: return sizeof(Sub<D3D12_DEPTH_STENCIL_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: return sizeof(Sub<D3D12_INPUT_LAYOUT_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: return sizeof(Sub<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: return sizeof(Sub<D3D12_PRIMITIVE_TOPOLOGY_TYPE>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: return sizeof(Sub<D3D12_RT_FORMAT_ARRAY>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: return sizeof(Sub<DXGI_FORMAT>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: return sizeof(Sub<DXGI_SAMPLE_DESC>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: return sizeof(Sub<UINT>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: return sizeof(Sub<D3D12_CACHED_PIPELINE_STATE>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: return sizeof(Sub<D3D12_PIPELINE_STATE_FLAGS>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: return sizeof(Sub<D3D12_DEPTH_STENCIL_DESC1>);
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: return sizeof(Sub<D3D12_VIEW_INSTANCING_DESC>);
        default: return 0;  // newer types: stop parsing
    }
}

// The pixel (or compute) shader id of a stream; 0 if none found. *unknownType = first type we can't size.
uint32_t StreamId(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, int* unknownType) {
    const uint8_t* p = static_cast<const uint8_t*>(desc->pPipelineStateSubobjectStream);
    const uint8_t* end = p + desc->SizeInBytes;
    uint32_t id = 0;
    while (p + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= end) {
        const auto t = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(p);
        const size_t n = SubSize(t);
        if (!n) {
            *unknownType = int(t);
            break;
        }
        if (t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS || t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS) {
            const auto& bc = reinterpret_cast<const Sub<D3D12_SHADER_BYTECODE>*>(p)->data;
            if (bc.pShaderBytecode && bc.BytecodeLength) id = Fnv(bc.pShaderBytecode, bc.BytecodeLength);
        }
        p += n;
    }
    return id;
}

std::atomic<uint64_t> g_streamIds{0};

void RecordStream(void* pso, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, const char* kind) {
    int unknownType = -1;
    const uint32_t id = StreamId(desc, &unknownType);
    if (id) ++g_streamIds;
    static std::atomic<int> warned{0};
    if (unknownType >= 0 && warned++ < 5) Log("stream subobject type %d not parsed (%s)", unknownType, kind);
    RecordPso(pso, id, kind);
}

HRESULT STDMETHODCALLTYPE HkCreatePsoStream(ID3D12Device2* d, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** out) {
    const HRESULT hr = Orig<decltype(&HkCreatePsoStream)>(kCreatePsoStream)(d, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        if (g_streamPsos++ == 0) Log("first stream PSO (CreatePipelineState)");
        RecordStream(*out, desc, "stream");
    }
    return hr;
}

std::atomic<uint64_t> g_libGfx{0}, g_libCs{0}, g_libStream{0}, g_libMiss{0};

HRESULT STDMETHODCALLTYPE HkLibLoadGfx(ID3D12PipelineLibrary* lib, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid,
                                       void** out) {
    const HRESULT hr = Orig<decltype(&HkLibLoadGfx)>(kLibLoadGfx)(lib, name, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ++g_libGfx;
        const uint32_t id = desc->PS.pShaderBytecode ? Fnv(desc->PS.pShaderBytecode, desc->PS.BytecodeLength) : 0;
        RecordPso(*out, id, "graphics from library");
    } else {
        ++g_libMiss;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HkLibLoadCs(ID3D12PipelineLibrary* lib, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid,
                                      void** out) {
    const HRESULT hr = Orig<decltype(&HkLibLoadCs)>(kLibLoadCs)(lib, name, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ++g_libCs;
        const uint32_t id = desc->CS.pShaderBytecode ? Fnv(desc->CS.pShaderBytecode, desc->CS.BytecodeLength) : 0;
        RecordPso(*out, id, "compute from library");
    } else {
        ++g_libMiss;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HkLibLoadStream(ID3D12PipelineLibrary1* lib, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid,
                                          void** out) {
    const HRESULT hr = Orig<decltype(&HkLibLoadStream)>(kLibLoadStream)(lib, name, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ++g_libStream;
        RecordStream(*out, desc, "stream from library");
    } else {
        ++g_libMiss;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HkCreatePsoLib(ID3D12Device1* d, const void* blob, SIZE_T size, REFIID riid, void** out) {
    const HRESULT hr = Orig<decltype(&HkCreatePsoLib)>(kCreatePsoLib)(d, blob, size, riid, out);
    ++g_psoLibs;
    if (SUCCEEDED(hr) && out && *out) {
        void** vt = *reinterpret_cast<void***>(*out);
        Log("CreatePipelineLibrary (%zu bytes): library vtable %p in %s, hooking its Load* methods", size, vt, ModuleOf(vt[0]).c_str());
        Patch(vt, kLibLoadGfx, &HkLibLoadGfx);
        Patch(vt, kLibLoadCs, &HkLibLoadCs);
        IUnknown* tmp = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(*out)->QueryInterface(__uuidof(ID3D12PipelineLibrary1), reinterpret_cast<void**>(&tmp)))) {
            Patch(vt, kLibLoadStream, &HkLibLoadStream);
            tmp->Release();
        }
    } else {
        Log("CreatePipelineLibrary (%zu bytes) -> %08lx", size, hr);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HkCreateRootSig(ID3D12Device* d, UINT mask, const void* blob, SIZE_T size, REFIID riid, void** out) {
    ++g_rootSigs;
    return Orig<decltype(&HkCreateRootSig)>(kCreateRootSig)(d, mask, blob, size, riid, out);
}

void STDMETHODCALLTYPE HkCreateSrv(ID3D12Device* d, ID3D12Resource* r, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    ++g_srvs;
    Orig<decltype(&HkCreateSrv)>(kCreateSrv)(d, r, desc, h);
}

void STDMETHODCALLTYPE HkCreateUav(ID3D12Device* d, ID3D12Resource* r, ID3D12Resource* c, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,
                                   D3D12_CPU_DESCRIPTOR_HANDLE h) {
    ++g_uavs;
    Orig<decltype(&HkCreateUav)>(kCreateUav)(d, r, c, desc, h);
}

void STDMETHODCALLTYPE HkCreateRtv(ID3D12Device* d, ID3D12Resource* r, const D3D12_RENDER_TARGET_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    ++g_rtvs;
    Orig<decltype(&HkCreateRtv)>(kCreateRtv)(d, r, desc, h);
}

void STDMETHODCALLTYPE HkCopyDescs(ID3D12Device* d, UINT nd, const D3D12_CPU_DESCRIPTOR_HANDLE* ds, const UINT* dsz, UINT ns,
                                   const D3D12_CPU_DESCRIPTOR_HANDLE* ss, const UINT* ssz, D3D12_DESCRIPTOR_HEAP_TYPE t) {
    ++g_copyDescs;
    Orig<decltype(&HkCopyDescs)>(kCopyDescs)(d, nd, ds, dsz, ns, ss, ssz, t);
}

void STDMETHODCALLTYPE HkCopyDescsSimple(ID3D12Device* d, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src,
                                         D3D12_DESCRIPTOR_HEAP_TYPE t) {
    ++g_copyDescsSimple;
    Orig<decltype(&HkCopyDescsSimple)>(kCopyDescsSimple)(d, n, dst, src, t);
}

// ---------------------------------------------------------------- command list / queue hooks
// D3D12Core has more than one command list class (each its own vtable): originals per vtable.
constexpr int kMaxListVts = 8;
std::atomic<void**> g_lvt[kMaxListVts] = {};
void* g_lorig[kMaxListVts][kSlotCount] = {};
std::mutex g_listMutex;

template <class F>
F LOrig(void* list, Slot s) {
    void** vt = *reinterpret_cast<void***>(list);
    for (int i = 0; i < kMaxListVts; ++i)
        if (g_lvt[i].load(std::memory_order_acquire) == vt) return reinterpret_cast<F>(g_lorig[i][s]);
    return nullptr;  // unreachable: only patched vtables call our hooks
}

void PatchList(int i, void** vt, Slot s, void* hook) {
    DWORD old;
    void** slot = vt + g_slot[s];
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
    g_lorig[i][s] = *slot;
    *slot = hook;
    VirtualProtect(slot, sizeof(void*), old, &old);
}

void STDMETHODCALLTYPE HkDrawInst(ID3D12GraphicsCommandList* l, UINT a, UINT b, UINT c, UINT e) {
    ++g_draws;
    if (!g_firstDraw.exchange(true)) Log("first draw recorded");
    LOrig<decltype(&HkDrawInst)>(l, kDrawInst)(l, a, b, c, e);
}

void STDMETHODCALLTYPE HkDrawIndexed(ID3D12GraphicsCommandList* l, UINT a, UINT b, UINT c, INT d, UINT e) {
    ++g_drawsIndexed;
    LOrig<decltype(&HkDrawIndexed)>(l, kDrawIndexed)(l, a, b, c, d, e);
}

void STDMETHODCALLTYPE HkDispatch(ID3D12GraphicsCommandList* l, UINT x, UINT y, UINT z) {
    ++g_dispatches;
    LOrig<decltype(&HkDispatch)>(l, kDispatch)(l, x, y, z);
}

void STDMETHODCALLTYPE HkExecIndirect(ID3D12GraphicsCommandList* l, ID3D12CommandSignature* s, UINT n, ID3D12Resource* a, UINT64 ao,
                                      ID3D12Resource* c, UINT64 co) {
    ++g_indirect;
    LOrig<decltype(&HkExecIndirect)>(l, kExecIndirect)(l, s, n, a, ao, c, co);
}

void STDMETHODCALLTYPE HkSetPso(ID3D12GraphicsCommandList* l, ID3D12PipelineState* pso) {
    ++g_setPso;
    if (pso) {
        uint32_t id = 0;
        bool known;
        {
            std::shared_lock<std::shared_mutex> lock(g_psoMutex);
            auto it = g_psoIds.find(pso);
            known = it != g_psoIds.end();
            if (known) id = it->second;
        }
        if (!known) {
            ++g_unknownBinds;
            std::unique_lock<std::shared_mutex> lock(g_psoMutex);
            if (g_unknownPsos.insert(pso).second && g_unknownPsos.size() <= 10)
                Log("bound a PSO we never saw created: %p (list vtable %s)", pso, ModuleOf(*reinterpret_cast<void**>(l)).c_str());
        } else if (id) {
            for (Watch& w : g_watch)
                if (w.id == id) ++w.bound;
        }
    }
    LOrig<decltype(&HkSetPso)>(l, kSetPso)(l, pso);
}

void STDMETHODCALLTYPE HkExecLists(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    g_execLists += n;
    if (!g_firstExec.exchange(true)) Log("first ExecuteCommandLists");
    Orig<decltype(&HkExecLists)>(kExecLists)(q, n, lists);
}

void HookList(void* list) {
    void** vt = *reinterpret_cast<void***>(list);
    std::lock_guard<std::mutex> lock(g_listMutex);
    int i = 0;
    for (; i < kMaxListVts && g_lvt[i].load(); ++i)
        if (g_lvt[i].load() == vt) return;
    if (i == kMaxListVts) {
        Log("more than %d command list vtables: %p not hooked", kMaxListVts, vt);
        return;
    }
    Log("command list vtable #%d %p in %s: hooking draws/dispatch/SetPipelineState/ExecuteIndirect", i, vt, ModuleOf(vt[0]).c_str());
    // Originals recorded before the vtable is published (and before any slot points at us).
    for (Slot s : {kDrawInst, kDrawIndexed, kDispatch, kSetPso, kExecIndirect}) g_lorig[i][s] = vt[g_slot[s]];
    g_lvt[i].store(vt, std::memory_order_release);
    PatchList(i, vt, kDrawInst, reinterpret_cast<void*>(&HkDrawInst));
    PatchList(i, vt, kDrawIndexed, reinterpret_cast<void*>(&HkDrawIndexed));
    PatchList(i, vt, kDispatch, reinterpret_cast<void*>(&HkDispatch));
    PatchList(i, vt, kSetPso, reinterpret_cast<void*>(&HkSetPso));
    PatchList(i, vt, kExecIndirect, reinterpret_cast<void*>(&HkExecIndirect));
}

HRESULT STDMETHODCALLTYPE HkCreateList(ID3D12Device* d, UINT mask, D3D12_COMMAND_LIST_TYPE t, ID3D12CommandAllocator* a,
                                       ID3D12PipelineState* p, REFIID riid, void** out) {
    const HRESULT hr = Orig<decltype(&HkCreateList)>(kCreateList)(d, mask, t, a, p, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ++g_lists;
        HookList(*out);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HkCreateList1(ID3D12Device4* d, UINT mask, D3D12_COMMAND_LIST_TYPE t, D3D12_COMMAND_LIST_FLAGS f, REFIID riid,
                                        void** out) {
    const HRESULT hr = Orig<decltype(&HkCreateList1)>(kCreateList1)(d, mask, t, f, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ++g_lists;
        HookList(*out);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HkCreateQueue(ID3D12Device* d, const D3D12_COMMAND_QUEUE_DESC* desc, REFIID riid, void** out) {
    const HRESULT hr = Orig<decltype(&HkCreateQueue)>(kCreateQueue)(d, desc, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        void** vt = *reinterpret_cast<void***>(*out);
        Log("command queue created (type %d, vtable %p in %s)", desc->Type, vt, ModuleOf(vt[0]).c_str());
        if (!g_queueVt) {
            g_queueVt = vt;
            Patch(vt, kExecLists, &HkExecLists);
        }
    }
    return hr;
}

void HookDevice(IUnknown* unk, const char* how) {
    ID3D12Device* dev = nullptr;
    if (FAILED(unk->QueryInterface(IID_PPV_ARGS(&dev)))) return;
    void** vt = *reinterpret_cast<void***>(dev);
    Log("D3D12 device %p (%s), vtable %p in %s; loaded: %s", dev, how, vt, ModuleOf(vt[0]).c_str(), Loaded().c_str());
    if (g_devVt) {
        if (vt != g_devVt) Log("  a second device class: not hooked");
        dev->Release();
        return;
    }
    g_devVt = vt;
    Patch(vt, kCreateQueue, &HkCreateQueue);
    Patch(vt, kCreateGfxPso, &HkCreateGfxPso);
    Patch(vt, kCreateCsPso, &HkCreateCsPso);
    Patch(vt, kCreateList, &HkCreateList);
    Patch(vt, kCreateRootSig, &HkCreateRootSig);
    Patch(vt, kCreateSrv, &HkCreateSrv);
    Patch(vt, kCreateUav, &HkCreateUav);
    Patch(vt, kCreateRtv, &HkCreateRtv);
    Patch(vt, kCopyDescs, &HkCopyDescs);
    Patch(vt, kCopyDescsSimple, &HkCopyDescsSimple);
    IUnknown* tmp = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device1), reinterpret_cast<void**>(&tmp)))) {
        Patch(vt, kCreatePsoLib, &HkCreatePsoLib);
        tmp->Release();
    }
    if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device2), reinterpret_cast<void**>(&tmp)))) {
        Patch(vt, kCreatePsoStream, &HkCreatePsoStream);
        tmp->Release();
    }
    if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device4), reinterpret_cast<void**>(&tmp)))) {
        Patch(vt, kCreateList1, &HkCreateList1);
        tmp->Release();
    }
    Log("device hooks installed");
    dev->Release();
}

// ---------------------------------------------------------------- D3D12CreateDevice / DXGI
using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
CreateDeviceFn g_realCreateDevice = nullptr;

HRESULT WINAPI HkD3D12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL fl, REFIID riid, void** out) {
    const HRESULT hr = g_realCreateDevice(adapter, fl, riid, out);
    Log("game D3D12CreateDevice(level %x, out %s) -> %08lx", fl, out ? "yes" : "null (support check)", hr);
    if (SUCCEEDED(hr) && out && *out) HookDevice(static_cast<IUnknown*>(*out), "game's D3D12CreateDevice");
    return hr;
}

using CreateSwapHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                     const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

HRESULT STDMETHODCALLTYPE HkCreateSwapHwnd(IDXGIFactory2* f, IUnknown* queue, HWND wnd, const DXGI_SWAP_CHAIN_DESC1* desc,
                                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* out, IDXGISwapChain1** sc) {
    const HRESULT hr = Orig<CreateSwapHwndFn>(kCreateSwapHwnd)(f, queue, wnd, desc, fs, out, sc);
    void** qvt = queue ? *reinterpret_cast<void***>(queue) : nullptr;
    Log("CreateSwapChainForHwnd %ux%u fmt %d buffers %u -> %08lx; queue vtable in %s, swapchain vtable in %s", desc->Width, desc->Height,
        desc->Format, desc->BufferCount, hr, qvt ? ModuleOf(qvt[0]).c_str() : "-",
        SUCCEEDED(hr) && sc && *sc ? ModuleOf((*reinterpret_cast<void***>(*sc))[0]).c_str() : "-");
    return hr;
}

void HookFactory(void* factory, const char* how) {
    void** vt = *reinterpret_cast<void***>(factory);
    Log("DXGI factory (%s), vtable %p in %s", how, vt, ModuleOf(vt[0]).c_str());
    if (!g_factoryVt) {
        g_factoryVt = vt;
        Patch(vt, kCreateSwapHwnd, &HkCreateSwapHwnd);
    } else if (vt != g_factoryVt) {
        Log("  another factory class (not hooked)");
    }
}

using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
using CreateFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
CreateFactory1Fn g_realFactory1 = nullptr;
CreateFactory2Fn g_realFactory2 = nullptr;

HRESULT WINAPI HkCreateFactory1(REFIID riid, void** out) {
    const HRESULT hr = g_realFactory1(riid, out);
    if (SUCCEEDED(hr) && out && *out) HookFactory(*out, "game's CreateDXGIFactory1");
    return hr;
}

HRESULT WINAPI HkCreateFactory2(UINT flags, REFIID riid, void** out) {
    const HRESULT hr = g_realFactory2(flags, riid, out);
    if (SUCCEEDED(hr) && out && *out) HookFactory(*out, "game's CreateDXGIFactory2");
    return hr;
}

using GetProcFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
GetProcFn g_realGetProc = nullptr;

FARPROC WINAPI HkGetProcAddress(HMODULE m, LPCSTR name) {
    FARPROC p = g_realGetProc(m, name);
    if (p && reinterpret_cast<uintptr_t>(name) > 0xFFFF) {
        if (!strcmp(name, "D3D12CreateDevice")) {
            Log("game GetProcAddress(D3D12CreateDevice) from %s -> %s", ModuleOf(m).c_str(), ModuleOf(reinterpret_cast<void*>(p)).c_str());
            if (!g_realCreateDevice) g_realCreateDevice = reinterpret_cast<CreateDeviceFn>(p);
            if (reinterpret_cast<CreateDeviceFn>(p) == g_realCreateDevice) return reinterpret_cast<FARPROC>(&HkD3D12CreateDevice);
        } else if (!strncmp(name, "D3D12", 5) || !strncmp(name, "CreateDXGI", 10)) {
            Log("game GetProcAddress(%s) from %s", name, ModuleOf(m).c_str());
        }
    }
    return p;
}

// Replaces every slot of re2.exe's import table that holds `target`.
int PatchIat(void* target, void* hook) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleA(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int n = 0;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); dir.VirtualAddress && imp->Name; ++imp) {
        for (auto* t = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk); t->u1.Function; ++t) {
            if (reinterpret_cast<void*>(t->u1.Function) != target) continue;
            DWORD old;
            if (VirtualProtect(&t->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                t->u1.Function = reinterpret_cast<ULONGLONG>(hook);
                VirtualProtect(&t->u1.Function, sizeof(void*), old, &old);
                ++n;
            }
        }
    }
    return n;
}

void InstallEarly() {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    g_realGetProc = reinterpret_cast<GetProcFn>(GetProcAddress(k32, "GetProcAddress"));
    int n = PatchIat(reinterpret_cast<void*>(g_realGetProc), reinterpret_cast<void*>(&HkGetProcAddress));
    // KERNELBASE's export too (some imports resolve there).
    if (HMODULE kb = GetModuleHandleA("kernelbase.dll")) {
        void* p = reinterpret_cast<void*>(GetProcAddress(kb, "GetProcAddress"));
        if (p != reinterpret_cast<void*>(g_realGetProc)) n += PatchIat(p, reinterpret_cast<void*>(&HkGetProcAddress));
    }
    Log("re2.exe GetProcAddress import slots patched: %d", n);

    if (HMODULE dxgi = GetModuleHandleA("dxgi.dll")) {
        g_realFactory1 = reinterpret_cast<CreateFactory1Fn>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
        g_realFactory2 = reinterpret_cast<CreateFactory2Fn>(GetProcAddress(dxgi, "CreateDXGIFactory2"));
        const int a = g_realFactory1 ? PatchIat(reinterpret_cast<void*>(g_realFactory1), reinterpret_cast<void*>(&HkCreateFactory1)) : 0;
        const int b = g_realFactory2 ? PatchIat(reinterpret_cast<void*>(g_realFactory2), reinterpret_cast<void*>(&HkCreateFactory2)) : 0;
        Log("re2.exe CreateDXGIFactory1/2 import slots patched: %d/%d (dxgi.dll here = %s)", a, b, ModuleOf(dxgi).c_str());
    }
}

// ---------------------------------------------------------------- REFramework plugin
struct PluginVersion {
    int major, minor, patch;
    const char* game_name;
};
using PresentCb = void (*)();
struct PluginFunctions {
    void* on_lua_state_created;
    void* on_lua_state_destroyed;
    bool (*on_present)(PresentCb);
    // (rest unused)
};
struct InitializeParam {
    void* reframework_module;
    const PluginVersion* version;
    const PluginFunctions* functions;
};

LARGE_INTEGER g_qpf, g_lastQpc;
uint64_t g_last[8] = {};

void OnPresent() {
    const uint32_t n = ++g_presents;
    if (n == 1) Log("first present seen by REFramework's on_present (plugin initialized)");
    if (n % 600) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double sec = g_lastQpc.QuadPart ? double(now.QuadPart - g_lastQpc.QuadPart) / g_qpf.QuadPart : 0;
    g_lastQpc = now;
    const uint64_t cur[8] = {g_draws, g_drawsIndexed, g_dispatches, g_setPso, g_indirect, g_execLists, g_unknownBinds, g_srvs + g_uavs + g_rtvs};
    double per[8];
    for (int i = 0; i < 8; ++i) per[i] = double(cur[i] - g_last[i]) / 600.0, g_last[i] = cur[i];
    size_t unknown;
    {
        std::shared_lock<std::shared_mutex> lock(g_psoMutex);
        unknown = g_unknownPsos.size();
    }
    Log("present %u (%.0f fps): per frame draws %.0f, indexed %.0f, dispatch %.0f, SetPipelineState %.0f, ExecuteIndirect %.0f, "
        "lists executed %.0f, views created %.0f, unknown PSO binds %.1f | total PSOs gfx %llu cs %llu stream %llu, libs %llu, "
        "root sigs %llu, distinct unknown PSOs %zu, CopyDescriptors %llu/%llu",
        n, sec > 0 ? 600.0 / sec : 0.0, per[0], per[1], per[2], per[3], per[4], per[5], per[7], per[6], (unsigned long long)g_gfxPsos.load(),
        (unsigned long long)g_csPsos.load(), (unsigned long long)g_streamPsos.load(), (unsigned long long)g_psoLibs.load(),
        (unsigned long long)g_rootSigs.load(), unknown, (unsigned long long)g_copyDescs.load(), (unsigned long long)g_copyDescsSimple.load());
    std::string w;
    for (Watch& x : g_watch) {
        char b[96];
        snprintf(b, sizeof(b), "%s%s c%llu b%llu", w.empty() ? "" : ", ", x.name, (unsigned long long)x.created.load(), (unsigned long long)x.bound.load());
        w += b;
    }
    Log("  stream PSOs with a shader id %llu; library loads gfx %llu cs %llu stream %llu, misses %llu; watched shaders (created/bound): %s", (unsigned long long)g_streamIds.load(),
        (unsigned long long)g_libGfx.load(), (unsigned long long)g_libCs.load(), (unsigned long long)g_libStream.load(), (unsigned long long)g_libMiss.load(), w.c_str());
}

}  // namespace

extern "C" __declspec(dllexport) void reframework_plugin_required_version(PluginVersion* v) {
    v->major = 1, v->minor = 0, v->patch = 0;
}

extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const InitializeParam* param) {
    Log("reframework_plugin_initialize; loaded: %s", Loaded().c_str());
    QueryPerformanceFrequency(&g_qpf);
    if (param && param->functions && param->functions->on_present) param->functions->on_present(&OnPresent);
    return true;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        FILETIME c, e, k, u;
        GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
        g_t0 = (ULONGLONG(c.dwHighDateTime) << 32) | c.dwLowDateTime;
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (char* s = strrchr(path, '\\')) strcpy(s + 1, "RE2Probe.log");
        g_log = fopen(path, "w");
        ProbeSlots(g_slot);
        Log("RE2Probe DllMain (times = ms since process start); loaded: %s", Loaded().c_str());
        Log("slots: queue %d gfxPso %d csPso %d list %d psoLib %d psoStream %d list1 %d draw %d drawIdx %d dispatch %d setPso %d "
            "execIndirect %d execLists %d swapHwnd %d",
            g_slot[kCreateQueue], g_slot[kCreateGfxPso], g_slot[kCreateCsPso], g_slot[kCreateList], g_slot[kCreatePsoLib],
            g_slot[kCreatePsoStream], g_slot[kCreateList1], g_slot[kDrawInst], g_slot[kDrawIndexed], g_slot[kDispatch], g_slot[kSetPso],
            g_slot[kExecIndirect], g_slot[kExecLists], g_slot[kCreateSwapHwnd]);
        InstallEarly();
    }
    return TRUE;
}
