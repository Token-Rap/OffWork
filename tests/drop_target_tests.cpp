#include "drop_target.h"
#include "check.h"

#include <shellapi.h>
#include <shlobj.h>

#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

// Only the file-drop format is implemented; the source retains its own data.
class FileData final : public IDataObject {
public:
    std::vector<std::wstring> paths;
    bool hasFormat = true;
    bool unreadable = false;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return --references_; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        return hasFormat && format->cfFormat == CF_HDROP &&
               format->tymed == TYMED_HGLOBAL && format->dwAspect == DVASPECT_CONTENT
            ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (FAILED(QueryGetData(format))) return DV_E_FORMATETC;
        if (unreadable) return E_FAIL;
        std::wstring list;
        for (const auto& path : paths) { list += path; list += L'\0'; }
        list += L'\0';
        const auto bytes = list.size() * sizeof(wchar_t);
        HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + bytes);
        if (!storage) return E_OUTOFMEMORY;
        auto* files = static_cast<DROPFILES*>(GlobalLock(storage));
        if (!files) { GlobalFree(storage); return E_OUTOFMEMORY; }
        files->pFiles = sizeof(DROPFILES);
        files->fWide = TRUE;
        std::memcpy(reinterpret_cast<char*>(files) + sizeof(DROPFILES), list.data(), bytes);
        GlobalUnlock(storage);
        *medium = {};
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = storage;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    ULONG references_ = 1;
};

std::vector<bool> hoverChanges;
std::vector<std::wstring> drops;
void Hover(bool active) { hoverChanges.push_back(active); }
void Drop(const std::wstring& path) { drops.push_back(path); }

}  // namespace

int main(int argc, char** argv) {
    using offwork::ScreenshotDropTarget;
    CHECK(argc == 2);
    const auto image = std::filesystem::absolute(argv[1]).wstring();
    auto* target = new ScreenshotDropTarget(Hover, Drop);
    void* unknown = nullptr;
    CHECK(target->QueryInterface(IID_IUnknown, &unknown) == S_OK);
    CHECK(unknown == static_cast<IDropTarget*>(target));
    CHECK(static_cast<IUnknown*>(unknown)->Release() == 1);

    FileData data;
    const auto rejected = [&](DWORD allowed = DROPEFFECT_COPY) {
        hoverChanges.clear();
        DWORD effect = allowed;
        CHECK(target->DragEnter(&data, 0, {}, &effect) == S_OK);
        CHECK(effect == DROPEFFECT_NONE);
        effect = allowed;
        CHECK(target->DragOver(0, {}, &effect) == S_OK);
        CHECK(effect == DROPEFFECT_NONE);
        CHECK(target->DragLeave() == S_OK);
        CHECK(hoverChanges.empty());
    };
    rejected(); // No files.
    data.paths = {image, image}; rejected();
    data.paths = {L"C:\\unsupported.gif"}; rejected();
    data.paths = {L"C:\\missing-offwork-screenshot.png"}; rejected();
    const auto directory = std::filesystem::temp_directory_path() /
        (L"offwork-drop-" + std::to_wstring(GetCurrentProcessId()) + L".png");
    CHECK(std::filesystem::create_directory(directory));
    data.paths = {directory.wstring()}; rejected();
    CHECK(std::filesystem::remove(directory));
    data.paths = {image};
    data.hasFormat = false; rejected();
    data.hasFormat = true;
    data.unreadable = true; rejected();
    data.unreadable = false;
    rejected(DROPEFFECT_MOVE); // Never tell a source to delete/move its file.

    DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    CHECK(target->DragEnter(&data, 0, {}, &effect) == S_OK);
    CHECK(effect == DROPEFFECT_COPY && hoverChanges == std::vector<bool>{true});
    effect = DROPEFFECT_COPY;
    CHECK(target->DragOver(0, {}, &effect) == S_OK);
    CHECK(hoverChanges.size() == 1); // No redundant repaint on every move.
    CHECK(target->DragLeave() == S_OK);
    CHECK((hoverChanges == std::vector<bool>{true, false}));
    CHECK(drops.empty());

    hoverChanges.clear();
    effect = DROPEFFECT_COPY;
    CHECK(target->DragEnter(&data, 0, {}, &effect) == S_OK);
    effect = DROPEFFECT_COPY;
    CHECK(target->Drop(&data, 0, {}, &effect) == S_OK);
    CHECK(effect == DROPEFFECT_COPY && drops == std::vector<std::wstring>{image});
    CHECK((hoverChanges == std::vector<bool>{true, false}));

    // A data source changing its files between enter and drop must be rejected.
    hoverChanges.clear();
    effect = DROPEFFECT_COPY;
    CHECK(target->DragEnter(&data, 0, {}, &effect) == S_OK);
    data.paths.push_back(image);
    effect = DROPEFFECT_COPY;
    CHECK(target->Drop(&data, 0, {}, &effect) == S_OK);
    CHECK(effect == DROPEFFECT_NONE && drops.size() == 1);
    CHECK((hoverChanges == std::vector<bool>{true, false}));
    CHECK(target->Release() == 0);
    std::cout << "OLE drop target tests passed.\n";
}
