#pragma once
// engine/string_packer.lua (file helpers) and engine/save_manager.lua (the save thread).
// Files are the original's: STR_PACK text compressed with raw DEFLATE (love.data.compress
// 'deflate'), so saves move between Vitalatro and the PC game unchanged.
#include <string>

#include "core/value.h"

namespace vl {

// load_local_file(_file): the unpacked table, nil when missing or unreadable
Value load_local_file(const std::string &file);
// get_compressed(_file): the file's text, decompressed when needed; false when missing
bool get_compressed(const std::string &file, std::string &out);
// G.SAVE_MANAGER.request_channel:push(request): the table is packed now (on this thread),
// compressed and written to the memory card by the save thread
void save_manager_request(const TableRef &request);

}  // namespace vl
