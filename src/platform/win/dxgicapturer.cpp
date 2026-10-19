#include "winplatform.h"

#include "platform/hdrtonemap.h"

#include <QImage>
#include <QScreen>
#include <vector>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct DxgiScreenCapturer::Output {
    HMONITOR monitor = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging;
    QImage lastFrame;
    // 开了 HDR 的显示器：按半精度浮点的 scRGB 复制，再转成 sRGB（否则系统给的 8 位画面发白、过曝）
    bool hdr = false;
    float whiteScale = 0; // 上次生成查找表时的“SDR 内容亮度”倍数
    std::vector<uint8_t> lut;
};

namespace {

using OutputPtr = std::unique_ptr<DxgiScreenCapturer::Output>;

// 这块显示器的“SDR 内容亮度”相对 80 尼特的倍数；取不到时按 1（不提亮）
float sdrWhiteScale(HMONITOR monitor)
{
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return 1.0f;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return 1.0f;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
        return 1.0f;
    for (UINT32 i = 0; i < pathCount; ++i) {
        const DISPLAYCONFIG_PATH_INFO& path = paths[i];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, mi.szDevice) != 0)
            continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof(white);
        white.header.adapterId = path.targetInfo.adapterId;
        white.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS && white.SDRWhiteLevel > 0)
            return float(white.SDRWhiteLevel) / 1000.0f; // 1000 表示 80 尼特
        break;
    }
    return 1.0f;
}

// 显示器当前是否开着 HDR（桌面按 HDR10 色彩空间输出）
bool isHdrOutput(IDXGIOutput* output)
{
    ComPtr<IDXGIOutput6> output6;
    DXGI_OUTPUT_DESC1 desc{};
    return SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6))) && SUCCEEDED(output6->GetDesc1(&desc))
           && desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
}

// 为指定显示器创建复制会话；旋转屏幕暂不支持（交给 GDI）
bool createDuplication(HMONITOR monitor, DxgiScreenCapturer::Output& out)
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || desc.Monitor != monitor)
                continue;
            if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED)
                return false;

            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1)))
                return false;
            if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                         D3D11_SDK_VERSION, &out.device, nullptr, &out.context)))
                return false;
            // HDR 显示器要半精度浮点的画面（老接口给的 8 位画面是系统粗暴截断的，发白过曝）
            ComPtr<IDXGIOutput5> output5;
            if (isHdrOutput(output.Get()) && SUCCEEDED(output.As(&output5))) {
                const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT};
                out.hdr = SUCCEEDED(output5->DuplicateOutput1(out.device.Get(), 0, 1, formats, &out.duplication));
            }
            // 双显卡笔记本上若显示器不由该适配器驱动，这里会失败
            if (!out.hdr && FAILED(output1->DuplicateOutput(out.device.Get(), &out.duplication)))
                return false;
            out.monitor = monitor;
            return true;
        }
    }
    return false;
}

} // namespace

DxgiScreenCapturer::DxgiScreenCapturer() = default;
DxgiScreenCapturer::~DxgiScreenCapturer() = default;

QPixmap DxgiScreenCapturer::grab(QScreen* screen)
{
    const HMONITOR monitor = monitorHandle(screen);
    if (!monitor)
        return {};

    Output* out = nullptr;
    for (const auto& o : m_outputs) {
        if (o->monitor == monitor)
            out = o.get();
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!out) {
            auto created = std::make_unique<Output>();
            if (!createDuplication(monitor, *created))
                return {};
            out = created.get();
            m_outputs.push_back(std::move(created));
        }

        // 已有上一帧时不等待：超时说明画面没有变化，直接复用。
        // 新建的会话第一帧可能只是空白（LastPresentTime 为 0），要等到真正的画面
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        HRESULT hr = S_OK;
        for (int wait = 0; wait < 6; ++wait) {
            hr = out->duplication->AcquireNextFrame(out->lastFrame.isNull() ? 50 : 0, &info, &resource);
            if (FAILED(hr) || info.LastPresentTime.QuadPart != 0 || !out->lastFrame.isNull())
                break;
            resource.Reset();
            out->duplication->ReleaseFrame();
            hr = DXGI_ERROR_WAIT_TIMEOUT;
        }

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // 静止的桌面可能一直没有新帧：没有缓存时交给 GDI
            if (out->lastFrame.isNull())
                return {};
            break;
        }
        if (FAILED(hr)) {
            // ACCESS_LOST：分辨率变化、切换到安全桌面等，重建会话再试一次
            m_outputs.erase(std::remove_if(m_outputs.begin(), m_outputs.end(),
                                           [out](const OutputPtr& o) { return o.get() == out; }),
                            m_outputs.end());
            out = nullptr;
            if (hr == DXGI_ERROR_ACCESS_LOST)
                continue;
            return {};
        }

        ComPtr<ID3D11Texture2D> texture;
        bool ok = SUCCEEDED(resource.As(&texture));
        D3D11_TEXTURE2D_DESC desc{};
        if (ok) {
            texture->GetDesc(&desc);
            ok = desc.Format == (out->hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM);
        }
        ok = ok && info.LastPresentTime.QuadPart != 0; // 只有鼠标变化的帧不更新缓存
        if (ok && !out->staging) {
            D3D11_TEXTURE2D_DESC sd = desc;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.BindFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sd.MiscFlags = 0;
            sd.MipLevels = 1;
            sd.ArraySize = 1;
            sd.SampleDesc = {1, 0};
            ok = SUCCEEDED(out->device->CreateTexture2D(&sd, nullptr, &out->staging));
        }
        if (ok)
            out->context->CopyResource(out->staging.Get(), texture.Get());
        out->duplication->ReleaseFrame();

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (ok && SUCCEEDED(out->context->Map(out->staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            const auto* data = static_cast<const uchar*>(mapped.pData);
            if (out->hdr) {
                // “SDR 内容亮度”随时可能被用户调整，每次截图都读一下，变了才重建查找表
                const float scale = sdrWhiteScale(monitor);
                if (scale != out->whiteScale) {
                    out->lut = makeScRgbToSrgbLut(scale);
                    out->whiteScale = scale;
                }
                out->lastFrame = scRgbToImage(data, int(desc.Width), int(desc.Height), int(mapped.RowPitch), out->lut);
            } else {
                out->lastFrame = QImage(data, int(desc.Width), int(desc.Height), int(mapped.RowPitch), QImage::Format_RGB32).copy();
            }
            out->context->Unmap(out->staging.Get(), 0);
        } else if (out->lastFrame.isNull()) {
            return {};
        }
        break;
    }

    if (!out || out->lastFrame.isNull() || out->lastFrame.size() != physicalScreenRect(screen).size())
        return {};
    QPixmap pixmap = QPixmap::fromImage(out->lastFrame);
    pixmap.setDevicePixelRatio(screen->devicePixelRatio());
    return pixmap;
}
