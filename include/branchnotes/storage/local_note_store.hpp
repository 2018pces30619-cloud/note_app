#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace branchnotes::storage {

struct NoteInfo {
    std::wstring title;
    std::wstring category;
    std::filesystem::path path;
    std::filesystem::file_time_type modified;
};

class LocalNoteStore {
public:
    LocalNoteStore();

    bool Initialize(std::wstring& error);
    const std::filesystem::path& RootPath() const noexcept;
    const std::wstring& DefaultCategory() const noexcept;

    std::vector<std::wstring> ListCategories() const;
    std::vector<NoteInfo> ListNotes(const std::wstring& category,
                                    const std::wstring& query = {}) const;

    bool CreateCategory(const std::wstring& name, std::wstring& error);
    bool RenameCategory(const std::wstring& oldName, const std::wstring& newName,
                        std::wstring& error);
    bool DeleteCategory(const std::wstring& name, std::wstring& error);

    bool CreateNote(const std::wstring& category, std::wstring& title,
                    std::wstring& error);
    bool RenameNote(const std::wstring& category, const std::wstring& oldTitle,
                    const std::wstring& newTitle, std::wstring& error);
    bool DeleteNote(const std::wstring& category, const std::wstring& title,
                    std::wstring& error);
    bool MoveNote(const std::wstring& fromCategory, const std::wstring& title,
                  const std::wstring& toCategory, std::wstring& error);
    bool ReadNoteContents(const std::wstring& category, const std::wstring& title,
                          std::vector<char>& data, std::wstring& error) const;
    bool WriteNoteContents(const std::wstring& category, const std::wstring& title,
                           const std::vector<char>& data, std::wstring& error) const;
    bool ReadMindMap(const std::wstring& category, const std::wstring& title,
                     std::vector<char>& data) const;
    bool WriteMindMap(const std::wstring& category, const std::wstring& title,
                      const std::vector<char>& data, std::wstring& error) const;
    int LoadThemeIndex() const;
    bool SaveThemeIndex(int index, std::wstring& error) const;

private:
    std::filesystem::path CategoryPath(const std::wstring& name) const;
    std::filesystem::path NotePath(const std::wstring& category,
                                   const std::wstring& title) const;
    std::filesystem::path MindMapPath(const std::wstring& category,
                                      const std::wstring& title) const;
    static bool ValidateName(const std::wstring& name, std::wstring& error);

    std::filesystem::path rootPath_;
    std::wstring defaultCategory_ = L"未分類";
};

} // namespace branchnotes::storage
