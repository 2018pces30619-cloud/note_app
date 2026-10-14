#include "branchnotes/storage/local_note_store.hpp"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <system_error>

namespace branchnotes::storage {
namespace {

constexpr wchar_t kNotesExtension[] = L".rtf";

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::wstring ErrorFromCode(const std::error_code& code) {
    (void)code;
    return L"檔案操作失敗，請檢查資料夾權限與檔案狀態。";
}

bool HasRtfExtension(const std::filesystem::path& path) {
    return Lower(path.extension().wstring()) == L".rtf";
}

} // namespace

LocalNoteStore::LocalNoteStore() {
    PWSTR knownFolder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                      nullptr, &knownFolder))) {
        rootPath_ = std::filesystem::path(knownFolder) / L"BranchNotes" / L"Notes";
        CoTaskMemFree(knownFolder);
    }
}

bool LocalNoteStore::Initialize(std::wstring& error) {
    if (rootPath_.empty()) {
        error = L"無法取得本機應用程式資料夾。";
        return false;
    }
    std::error_code code;
    std::filesystem::create_directories(CategoryPath(defaultCategory_), code);
    if (code) {
        error = ErrorFromCode(code);
        return false;
    }
    return true;
}

const std::filesystem::path& LocalNoteStore::RootPath() const noexcept {
    return rootPath_;
}

const std::wstring& LocalNoteStore::DefaultCategory() const noexcept {
    return defaultCategory_;
}

std::filesystem::path LocalNoteStore::CategoryPath(const std::wstring& name) const {
    return rootPath_ / name;
}

std::filesystem::path LocalNoteStore::NotePath(const std::wstring& category,
                                              const std::wstring& title) const {
    return CategoryPath(category) / (title + kNotesExtension);
}

std::filesystem::path LocalNoteStore::MindMapPath(const std::wstring& category,
                                                  const std::wstring& title) const {
    return CategoryPath(category) / (title + L".map");
}

namespace {
bool WriteAtomic(const std::filesystem::path& destination,
                 const std::vector<char>& data, std::wstring& error) {
    auto temporary = destination;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { error = L"無法寫入本機檔案。"; return false; }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            error = L"寫入本機檔案時發生錯誤。";
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = L"無法完成本機儲存，請檢查資料夾權限。";
        return false;
    }
    return true;
}
}

bool LocalNoteStore::ValidateName(const std::wstring& name, std::wstring& error) {
    if (name.empty() || name == L"." || name == L".." ||
        name.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
        error = L"名稱不能空白。";
        return false;
    }
    if (name.size() > 80) {
        error = L"名稱太長，請控制在 80 個字元以內。";
        return false;
    }
    if (name == L"全部筆記") {
        error = L"這個名稱保留給內建的全部筆記檢視。";
        return false;
    }
    if (name.back() == L'.' || name.back() == L' ') {
        error = L"名稱不能以句點或空格結尾。";
        return false;
    }
    if (name.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos ||
        std::any_of(name.begin(), name.end(), [](wchar_t ch) { return ch < 32; })) {
        error = L"名稱含有 Windows 不支援的字元。";
        return false;
    }
    const std::wstring stem = Lower(std::filesystem::path(name).stem().wstring());
    static const std::vector<std::wstring> reserved = {
        L"con", L"prn", L"aux", L"nul", L"com1", L"com2", L"com3", L"com4",
        L"com5", L"com6", L"com7", L"com8", L"com9", L"lpt1", L"lpt2",
        L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9"
    };
    if (std::find(reserved.begin(), reserved.end(), stem) != reserved.end()) {
        error = L"這是 Windows 保留名稱，請換一個名稱。";
        return false;
    }
    return true;
}

std::vector<std::wstring> LocalNoteStore::ListCategories() const {
    std::vector<std::wstring> categories;
    std::error_code code;
    for (std::filesystem::directory_iterator it(rootPath_, code), end;
         !code && it != end; it.increment(code)) {
        if (it->is_directory(code) && !code) categories.push_back(it->path().filename().wstring());
    }
    std::sort(categories.begin(), categories.end(), [](const auto& a, const auto& b) {
        const bool aDefault = a == L"未分類";
        const bool bDefault = b == L"未分類";
        if (aDefault != bDefault) return aDefault;
        return Lower(a) < Lower(b);
    });
    return categories;
}

std::vector<NoteInfo> LocalNoteStore::ListNotes(const std::wstring& category,
                                               const std::wstring& query) const {
    std::vector<NoteInfo> notes;
    std::error_code code;
    auto appendFrom = [&](const std::filesystem::path& folder,
                          const std::wstring& categoryName) {
        std::error_code localCode;
        if (!std::filesystem::exists(folder, localCode) || localCode) return;
        for (std::filesystem::directory_iterator it(folder, localCode), end;
             !localCode && it != end; it.increment(localCode)) {
            if (!it->is_regular_file(localCode) || localCode || !HasRtfExtension(it->path())) continue;
            const std::wstring title = it->path().stem().wstring();
            if (!query.empty() && Lower(title).find(Lower(query)) == std::wstring::npos) continue;
            notes.push_back({title, categoryName, it->path(), it->last_write_time(localCode)});
        }
    };

    if (category == L"全部筆記") {
        for (const auto& categoryName : ListCategories())
            appendFrom(CategoryPath(categoryName), categoryName);
    } else {
        appendFrom(CategoryPath(category), category);
    }
    std::sort(notes.begin(), notes.end(), [](const NoteInfo& a, const NoteInfo& b) {
        if (a.modified != b.modified) return a.modified > b.modified;
        return Lower(a.title) < Lower(b.title);
    });
    return notes;
}

bool LocalNoteStore::CreateCategory(const std::wstring& name, std::wstring& error) {
    if (!ValidateName(name, error)) return false;
    std::error_code code;
    if (!std::filesystem::create_directory(CategoryPath(name), code)) {
        error = code ? ErrorFromCode(code) : L"已經有同名分類。";
        return false;
    }
    return true;
}

bool LocalNoteStore::RenameCategory(const std::wstring& oldName,
                                    const std::wstring& newName,
                                    std::wstring& error) {
    if (oldName == defaultCategory_) { error = L"預設分類不能改名。"; return false; }
    if (!ValidateName(newName, error)) return false;
    if (Lower(oldName) == Lower(newName)) return true;
    if (!std::filesystem::exists(CategoryPath(oldName))) { error = L"找不到這個分類。"; return false; }
    std::error_code code;
    if (std::filesystem::exists(CategoryPath(newName), code)) { error = L"已經有同名分類。"; return false; }
    std::filesystem::rename(CategoryPath(oldName), CategoryPath(newName), code);
    if (code) { error = ErrorFromCode(code); return false; }
    return true;
}

bool LocalNoteStore::DeleteCategory(const std::wstring& name, std::wstring& error) {
    if (name == defaultCategory_) { error = L"預設分類不能刪除。"; return false; }
    const auto from = CategoryPath(name);
    if (!std::filesystem::exists(from)) { error = L"找不到這個分類。"; return false; }
    std::error_code code;
    const auto destination = CategoryPath(defaultCategory_);
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(from, code), end; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && !code) files.push_back(it->path());
    }
    if (code) { error = ErrorFromCode(code); return false; }
    for (const auto& file : files) {
        auto target = destination / file.filename();
        if (std::filesystem::exists(target, code)) {
            error = L"未分類中已有同名筆記。請先移動或改名後再刪除分類。";
            return false;
        }
    }
    for (const auto& file : files) {
        const auto target = destination / file.filename();
        std::filesystem::rename(file, target, code);
        if (code) { error = ErrorFromCode(code); return false; }
    }
    if (code) { error = ErrorFromCode(code); return false; }
    std::filesystem::remove(from, code);
    if (code) { error = ErrorFromCode(code); return false; }
    return true;
}

bool LocalNoteStore::CreateNote(const std::wstring& category, std::wstring& title,
                                std::wstring& error) {
    if (!std::filesystem::exists(CategoryPath(category))) { error = L"找不到所選分類。"; return false; }
    std::wstring base = L"新筆記";
    title = base;
    int suffix = 2;
    while (std::filesystem::exists(NotePath(category, title)))
        title = base + L" (" + std::to_wstring(suffix++) + L")";
    std::ofstream file(NotePath(category, title), std::ios::binary | std::ios::out);
    if (!file) { error = L"無法建立筆記檔案。"; return false; }
    constexpr char emptyRtf[] = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs22\\par}\r\n";
    file.write(emptyRtf, sizeof(emptyRtf) - 1);
    if (!file) { error = L"無法寫入筆記檔案。"; return false; }
    return true;
}

bool LocalNoteStore::RenameNote(const std::wstring& category,
                                const std::wstring& oldTitle,
                                const std::wstring& newTitle,
                                std::wstring& error) {
    if (!ValidateName(newTitle, error)) return false;
    if (Lower(oldTitle) == Lower(newTitle)) return true;
    const auto from = NotePath(category, oldTitle);
    const auto to = NotePath(category, newTitle);
    if (!std::filesystem::exists(from)) { error = L"找不到這篇筆記。"; return false; }
    if (std::filesystem::exists(to)) { error = L"這個分類已經有同名筆記。"; return false; }
    std::error_code code;
    std::filesystem::rename(from, to, code);
    if (code) { error = ErrorFromCode(code); return false; }
    const auto oldMap = MindMapPath(category, oldTitle);
    const auto newMap = MindMapPath(category, newTitle);
    if (std::filesystem::exists(oldMap)) {
        std::filesystem::rename(oldMap, newMap, code);
        if (code) {
            std::error_code rollback;
            std::filesystem::rename(to, from, rollback);
            error = ErrorFromCode(code);
            return false;
        }
    }
    return true;
}

bool LocalNoteStore::DeleteNote(const std::wstring& category,
                                const std::wstring& title,
                                std::wstring& error) {
    std::wstring paths = NotePath(category, title).wstring();
    paths.push_back(L'\0');
    const auto mapPath = MindMapPath(category, title);
    if (std::filesystem::exists(mapPath)) {
        paths += mapPath.wstring();
        paths.push_back(L'\0');
    }
    paths.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = paths.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    const int result = SHFileOperationW(&operation);
    if (result != 0 || operation.fAnyOperationsAborted) {
        error = L"無法將筆記移至資源回收筒。";
        return false;
    }
    return true;
}

bool LocalNoteStore::MoveNote(const std::wstring& fromCategory,
                              const std::wstring& title,
                              const std::wstring& toCategory,
                              std::wstring& error) {
    if (fromCategory == toCategory) return true;
    const auto from = NotePath(fromCategory, title);
    const auto to = NotePath(toCategory, title);
    if (!std::filesystem::exists(from)) { error = L"找不到這篇筆記。"; return false; }
    if (!std::filesystem::exists(CategoryPath(toCategory))) { error = L"找不到目標分類。"; return false; }
    if (std::filesystem::exists(to)) { error = L"目標分類已有同名筆記。"; return false; }
    const auto fromMap = MindMapPath(fromCategory, title);
    const auto toMap = MindMapPath(toCategory, title);
    const bool hasMap = std::filesystem::exists(fromMap);
    if (hasMap && std::filesystem::exists(toMap)) { error = L"目標分類已有同名心智圖。"; return false; }
    std::error_code code;
    std::filesystem::rename(from, to, code);
    if (code) { error = ErrorFromCode(code); return false; }
    if (hasMap) {
        std::filesystem::rename(fromMap, toMap, code);
        if (code) {
            std::error_code rollback;
            std::filesystem::rename(to, from, rollback);
            error = ErrorFromCode(code);
            return false;
        }
    }
    return true;
}

bool LocalNoteStore::ReadNoteContents(const std::wstring& category,
                                      const std::wstring& title,
                                      std::vector<char>& data,
                                      std::wstring& error) const {
    std::ifstream input(NotePath(category, title), std::ios::binary);
    if (!input) { error = L"無法讀取筆記內容。"; return false; }
    data.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad()) { error = L"讀取筆記時發生錯誤。"; return false; }
    return true;
}

bool LocalNoteStore::WriteNoteContents(const std::wstring& category,
                                       const std::wstring& title,
                                       const std::vector<char>& data,
                                       std::wstring& error) const {
    if (!std::filesystem::exists(NotePath(category, title))) {
        error = L"找不到要儲存的筆記。";
        return false;
    }
    return WriteAtomic(NotePath(category, title), data, error);
}

bool LocalNoteStore::ReadMindMap(const std::wstring& category,
                                 const std::wstring& title,
                                 std::vector<char>& data) const {
    std::ifstream input(MindMapPath(category, title), std::ios::binary);
    if (!input) { data.clear(); return false; }
    data.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !input.bad();
}

bool LocalNoteStore::WriteMindMap(const std::wstring& category,
                                  const std::wstring& title,
                                  const std::vector<char>& data,
                                  std::wstring& error) const {
    if (!std::filesystem::exists(NotePath(category, title))) {
        error = L"找不到要儲存心智圖的筆記。";
        return false;
    }
    return WriteAtomic(MindMapPath(category, title), data, error);
}

int LocalNoteStore::LoadThemeIndex() const {
    std::ifstream input(rootPath_ / L"theme.txt");
    int index = 0;
    if (input >> index) return std::clamp(index, 0, 4);
    return 0;
}

bool LocalNoteStore::SaveThemeIndex(int index, std::wstring& error) const {
    const std::string value = std::to_string(std::clamp(index, 0, 4));
    return WriteAtomic(rootPath_ / L"theme.txt", std::vector<char>(value.begin(), value.end()), error);
}

} // namespace branchnotes::storage
