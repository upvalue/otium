#pragma once

#include "ot/common.h"
#include "ot/user/local-storage.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

namespace filesystem {

// Maximum path length
constexpr size_t MAX_PATH_LENGTH = 256;

// Maximum open file handles per process
constexpr size_t MAX_OPEN_HANDLES = 32;

// File open flags
constexpr uintptr_t OPEN_READ = 0x01;
constexpr uintptr_t OPEN_WRITE = 0x02;
constexpr uintptr_t OPEN_CREATE = 0x04;
constexpr uintptr_t OPEN_TRUNCATE = 0x08;

enum class NodeType : uint8_t { FILE, DIRECTORY };

struct INode {
  uint32_t inode_num;
  NodeType type;
  ou::string name;
  uint32_t parent_inode;         // 0 for root
  ou::vector<uint8_t> data;      // File contents (empty for directories)
  ou::vector<uint32_t> children; // Child inode numbers (for directories)
  uint64_t created_time;
  uint64_t modified_time;

  INode() : inode_num(0), type(NodeType::FILE), parent_inode(0), created_time(0), modified_time(0) {}

  // Move constructor (needed because vectors don't have copy constructors)
  INode(INode &&other) = default;

  // Delete copy constructor
  INode(const INode &) = delete;
  INode &operator=(const INode &) = delete;
};

struct FileHandle {
  uint32_t handle_id;
  uint32_t inode_num;
  uintptr_t flags;
  bool is_open;

  FileHandle() : handle_id(0), inode_num(0), flags(0), is_open(false) {}
};

struct MemoryFilesystemStorage : public LocalStorage {
  ou::vector<INode> inodes;
  ou::vector<FileHandle> handles;
  uint32_t next_inode_num;
  uint32_t next_handle_id;

  MemoryFilesystemStorage() : next_inode_num(1), next_handle_id(1) {
    // Allocate enough pages for filesystem (50 pages = 200KB)
    process_storage_init(50);

    // Create root directory (inode 0)
    INode root;
    root.inode_num = 0;
    root.type = NodeType::DIRECTORY;
    root.name = "/";
    root.parent_inode = 0;
    root.created_time = 0;
    root.modified_time = 0;
    inodes.push_back(static_cast<INode &&>(root));
  }

  // Find inode by number
  INode *find_inode(uint32_t inode_num) {
    for (size_t i = 0; i < inodes.size(); i++) {
      if (inodes[i].inode_num == inode_num) {
        return &inodes[i];
      }
    }
    return nullptr;
  }

  // Find handle by ID
  FileHandle *find_handle(uint32_t handle_id) {
    for (size_t i = 0; i < handles.size(); i++) {
      if (handles[i].handle_id == handle_id && handles[i].is_open) {
        return &handles[i];
      }
    }
    return nullptr;
  }

  // Allocate a new handle
  FileHandle *allocate_handle() {
    // First try to reuse a closed handle slot
    for (size_t i = 0; i < handles.size(); i++) {
      if (!handles[i].is_open) {
        handles[i].handle_id = next_handle_id++;
        handles[i].is_open = true;
        return &handles[i];
      }
    }

    // If all handles are in use, check limit
    if (handles.size() >= MAX_OPEN_HANDLES) {
      return nullptr; // Too many open files
    }

    // Create a new handle
    FileHandle h;
    h.handle_id = next_handle_id++;
    h.is_open = true;
    handles.push_back(h);
    return &handles[handles.size() - 1];
  }
};

// Path resolution helper
struct PathComponents {
  ou::vector<ou::string> parts;
  bool is_absolute;

  PathComponents() : is_absolute(false) {}
};

// Split path into components (output parameter version to avoid copying)
inline void split_path(const ou::string &path, PathComponents &result) {
  result.parts.clear();
  result.is_absolute = false;

  if (path.empty()) {
    return;
  }

  // Check if absolute path
  result.is_absolute = (path[0] == '/');

  // Split by '/'
  size_t start = 0;
  if (result.is_absolute) {
    start = 1; // Skip leading '/'
  }

  for (size_t i = start; i <= path.length(); i++) {
    if (i == path.length() || path[i] == '/') {
      if (i > start) {
        // Extract component
        ou::string component;
        for (size_t j = start; j < i; j++) {
          component.push_back(path[j]);
        }

        // Skip empty components and "."
        if (!component.empty() && component != ".") {
          result.parts.push_back(component);
        }
      }
      start = i + 1;
    }
  }
}

} // namespace filesystem
