#pragma once

#include <ole2.h>

#include <atomic>
#include <string>

namespace offwork {

// OLE owns a reference between RegisterDragDrop and RevokeDragDrop. Callbacks
// run on the window's STA; no IDataObject or storage medium escapes a call.
class ScreenshotDropTarget final : public IDropTarget {
public:
    using HoverCallback = void (*)(bool);
    using DropCallback = void (*)(const std::wstring&);

    ScreenshotDropTarget(HoverCallback hover, DropCallback drop)
        : hover_(hover), drop_(drop) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD, POINTL, DWORD*) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD*) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD, POINTL, DWORD*) override;

private:
    void SetHover(bool active) noexcept;

    std::atomic<ULONG> references_{1};
    HoverCallback hover_;
    DropCallback drop_;
    bool supported_ = false;
    bool hovering_ = false;
};

}  // namespace offwork
