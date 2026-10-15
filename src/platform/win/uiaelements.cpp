#include "winplatform.h"

#include <ole2.h> // WIN32_LEAN_AND_MEAN 去掉了 COM 声明，MSVC 的 UIAutomation 头文件需要它
#include <uiautomation.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {
// 自行定义 GUID，避免依赖 MinGW / MSVC 的 uuid 导入库差异
const CLSID kClsidCUIAutomation = {0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};
const IID kIidIUIAutomation = {0x30cbe57d, 0xd9d0, 0x452a, {0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee}};
} // namespace

QList<QRect> collectUiaElements(HWND hwnd, int maxCount)
{
    QList<QRect> result;

    ComPtr<IUIAutomation> uia;
    if (FAILED(CoCreateInstance(kClsidCUIAutomation, nullptr, CLSCTX_INPROC_SERVER, kIidIUIAutomation,
                                reinterpret_cast<void**>(uia.GetAddressOf()))))
        return result;

    ComPtr<IUIAutomationElement> root;
    if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root)
        return result;

    // 一次跨进程调用取回所有可见控件及其矩形
    ComPtr<IUIAutomationCacheRequest> cache;
    if (FAILED(uia->CreateCacheRequest(&cache)))
        return result;
    cache->AddProperty(UIA_BoundingRectanglePropertyId);

    VARIANT notOffscreen;
    VariantInit(&notOffscreen);
    notOffscreen.vt = VT_BOOL;
    notOffscreen.boolVal = VARIANT_FALSE;
    ComPtr<IUIAutomationCondition> condition;
    if (FAILED(uia->CreatePropertyCondition(UIA_IsOffscreenPropertyId, notOffscreen, &condition)))
        return result;

    ComPtr<IUIAutomationElementArray> elements;
    if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, condition.Get(), cache.Get(), &elements)) || !elements)
        return result;

    int length = 0;
    elements->get_Length(&length);
    for (int i = 0; i < length && result.size() < maxCount; ++i) {
        ComPtr<IUIAutomationElement> el;
        RECT r{};
        if (FAILED(elements->GetElement(i, &el)) || FAILED(el->get_CachedBoundingRectangle(&r)))
            continue;
        const QRect rect(r.left, r.top, r.right - r.left, r.bottom - r.top);
        if (rect.width() >= 4 && rect.height() >= 4)
            result.append(rect);
    }
    return result;
}
