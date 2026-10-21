#include "drop_target.h"

#include "image_file.h"

#include <shellapi.h>

namespace offwork {
namespace {

std::wstring SingleImagePath(IDataObject* data) {
    if (data == nullptr) return {};
    FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    if (data->QueryGetData(&format) != S_OK) return {};
    struct Medium {
        STGMEDIUM value = {};
        ~Medium() { ReleaseStgMedium(&value); }
    } medium;
    if (FAILED(data->GetData(&format, &medium.value)) ||
        medium.value.tymed != TYMED_HGLOBAL || medium.value.hGlobal == nullptr) return {};

    const auto files = static_cast<HDROP>(medium.value.hGlobal);
    if (DragQueryFileW(files, 0xFFFFFFFF, nullptr, 0) != 1) return {};
    const UINT length = DragQueryFileW(files, 0, nullptr, 0);
    if (length == 0) return {};
    std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
    if (DragQueryFileW(files, 0, path.data(), length + 1) != length) return {};
    path.resize(length);
    if (!IsSupportedImageFile(path)) return {};
    // A directory with an image suffix is not an image file.
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return path;
}

}  // namespace

HRESULT ScreenshotDropTarget::QueryInterface(REFIID iid, void** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (iid != IID_IUnknown && iid != IID_IDropTarget) return E_NOINTERFACE;
    *object = static_cast<IDropTarget*>(this);
    AddRef();
    return S_OK;
}

ULONG ScreenshotDropTarget::AddRef() { return ++references_; }

ULONG ScreenshotDropTarget::Release() {
    const ULONG remaining = --references_;
    if (remaining == 0) delete this;
    return remaining;
}

void ScreenshotDropTarget::SetHover(bool active) noexcept {
    if (hovering_ == active) return;
    hovering_ = active;
    try { hover_(active); }
    catch (...) { /* Rendering failure must not unwind into the drag source. */ }
}

HRESULT ScreenshotDropTarget::DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) {
    if (effect == nullptr) return E_POINTER;
    const DWORD allowed = *effect;
    *effect = DROPEFFECT_NONE;
    try {
        supported_ = !SingleImagePath(data).empty();
        const bool accept = supported_ && (allowed & DROPEFFECT_COPY);
        SetHover(accept);
        if (accept) *effect = DROPEFFECT_COPY;
        return S_OK;
    } catch (...) {
        supported_ = false;
        SetHover(false);
        return E_UNEXPECTED;
    }
}

HRESULT ScreenshotDropTarget::DragOver(DWORD, POINTL, DWORD* effect) {
    if (effect == nullptr) return E_POINTER;
    const bool accept = supported_ && (*effect & DROPEFFECT_COPY);
    *effect = DROPEFFECT_NONE;
    try {
        SetHover(accept);
        if (accept) *effect = DROPEFFECT_COPY;
        return S_OK;
    } catch (...) { return E_UNEXPECTED; }
}

HRESULT ScreenshotDropTarget::DragLeave() {
    supported_ = false;
    try { SetHover(false); return S_OK; }
    catch (...) { return E_UNEXPECTED; }
}

HRESULT ScreenshotDropTarget::Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) {
    if (effect == nullptr) { DragLeave(); return E_POINTER; }
    const bool accept = supported_ && (*effect & DROPEFFECT_COPY);
    *effect = DROPEFFECT_NONE;
    supported_ = false;
    try {
        SetHover(false);
        // Revalidate: the source may have changed since DragEnter.
        const auto path = accept ? SingleImagePath(data) : std::wstring{};
        if (!path.empty()) {
            drop_(path);
            *effect = DROPEFFECT_COPY;
        }
        return S_OK;
    } catch (...) { return E_UNEXPECTED; }
}

}  // namespace offwork
