#include "WinInputCommon.h"
#include <cassert>

int main() {
    assert(key_code(L"SHIFT") == VK_SHIFT);
    assert(key_code(L"CTRL") == VK_CONTROL);
    assert(key_code(L"ALT") == VK_MENU);
    assert(key_code(L"ENTER") == VK_RETURN);
    assert(key_code(L"上") == VK_UP && key_code(L"下") == VK_DOWN);
    assert(key_code(L"左") == VK_LEFT && key_code(L"右") == VK_RIGHT);
    for (int n = 1; n <= 12; ++n)
        assert(key_code(L"F" + std::to_wstring(n)) == VK_F1 + n - 1);
    assert(key_code(L"F13") == 0 && key_code(L"F20") == 0);
    assert(utf8(L"").empty() && wide("").empty());
    std::string secret, command, value;
    assert(split_frame("secret\nkey\nENTER", secret, command, value));
    assert(secret == "secret" && command == "key" && value == "ENTER");
    assert(split_frame("secret\ntext\na\nb", secret, command, value) && value == "a\nb");
    WORD vk, scan; bool extended, down;
    assert(parse_key_event("162,29,1,1", vk, scan, extended, down));
    assert(vk == 162 && scan == 29 && extended && down);
    assert(parse_key_event("13,28,0,0", vk, scan, extended, down));
    assert(vk == 13 && scan == 28 && !extended && !down);
    assert(!parse_key_event("256,1,0,1", vk, scan, extended, down));
    assert(!parse_key_event("13,28,0,1junk", vk, scan, extended, down));
    std::string blob = clip_header(CF_UNICODETEXT, 0, "", 3) + "abc"
        + clip_header(0, 1, "WinInput.Test", 2) + "xy";
    size_t pos = 0; unsigned int id; bool named; std::string name, bytes;
    assert(clip_parse_one(blob, pos, id, named, name, bytes));
    assert(id == CF_UNICODETEXT && !named && name.empty() && bytes == "abc");
    assert(clip_parse_one(blob, pos, id, named, name, bytes));
    assert(id == 0 && named && name == "WinInput.Test" && bytes == "xy");
    assert(pos == blob.size());
    assert(!clip_parse_one(blob, pos, id, named, name, bytes));
    std::string truncated = clip_header(CF_UNICODETEXT, 0, "", 9) + "abc";
    size_t bad = 0;
    assert(!clip_parse_one(truncated, bad, id, named, name, bytes));
    std::vector<std::wstring> files = {L"C:\\a.txt", L"D:\\b.bin"};
    std::string dropfiles = clip_dropfiles_buffer(files);
    assert(clip_dropfiles_names(dropfiles).size() == 2);
    assert(clip_dropfiles_names(dropfiles)[0] == L"C:\\a.txt");
    assert(clip_dropfiles_names(dropfiles)[1] == L"D:\\b.bin");
    std::string transfer, rebuilt;
    if (clip_transfer_files(dropfiles, transfer)) {
        assert(clip_rebuild_files(transfer, rebuilt));
        assert(clip_dropfiles_names(rebuilt).size() == 2);
    }
    wchar_t temp_dir[MAX_PATH] = {};
    assert(GetTempPathW(MAX_PATH, temp_dir) != 0);
    std::wstring temp_file = std::wstring(temp_dir) + L"wininput_selftest.txt";
    HANDLE handle = CreateFileW(temp_file.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    assert(handle != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(handle, "hi", 2, &written, 0);
    CloseHandle(handle);
    std::string one = clip_dropfiles_buffer({temp_file});
    assert(clip_transfer_files(one, transfer));
    assert(clip_rebuild_files(transfer, rebuilt));
    std::vector<std::wstring> copied = clip_dropfiles_names(rebuilt);
    assert(copied.size() == 1);
    std::string content;
    assert(clip_read_file(copied[0], content) && content == "hi");
    DeleteFileW(temp_file.c_str());
    return 0;
}
