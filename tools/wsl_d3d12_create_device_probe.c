#include <stdint.h>
#include <stdio.h>
#include <dlfcn.h>

typedef int32_t HRESULT;
typedef uint32_t D3D_FEATURE_LEVEL;

struct guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

typedef HRESULT (*PFN_D3D12CreateDevice)(void *adapter,
                                         D3D_FEATURE_LEVEL minimum_feature_level,
                                         const struct guid *riid,
                                         void **device);

int main(void)
{
    static const struct guid iid_id3d12device = {
        0x189819f1,
        0x1db6,
        0x4b57,
        { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 },
    };
    void *lib;
    void *device = NULL;
    PFN_D3D12CreateDevice create_device;
    HRESULT hr;

    lib = dlopen("libd3d12.so", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL)
        lib = dlopen("/usr/lib/wsl/lib/libd3d12.so", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        printf("dlopen(libd3d12.so) failed: %s\n", dlerror());
        return 1;
    }
    create_device = (PFN_D3D12CreateDevice)dlsym(lib, "D3D12CreateDevice");
    if (create_device == NULL) {
        printf("dlsym(D3D12CreateDevice) failed: %s\n", dlerror());
        return 1;
    }

    hr = create_device(NULL, 0xb000, &iid_id3d12device, &device);
    printf("D3D12CreateDevice(NULL, FL11_0) hr=0x%08x device=%p\n",
           (uint32_t)hr, device);
    return hr < 0 ? 1 : 0;
}
