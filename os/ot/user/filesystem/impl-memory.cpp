#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/filesystem/types.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/user.hpp"

using namespace filesystem;

// Global storage for memory backend
static FilesystemStorage* g_storage = nullptr;

// Resolve path to inode number
static Result<uint32_t, ErrorCode> resolve_path(const ou::string& path) {
  if (path.length() > MAX_PATH_LENGTH) {
    return Result<uint32_t, ErrorCode>::err(FILESYSTEM__PATH_TOO_LONG);
  }

  PathComponents components;
  split_path(path, components);

  // Start from root
  uint32_t current_inode = 0;

  // Traverse path components
  for (size_t i = 0; i < components.parts.size(); i++) {
    const ou::string& name = components.parts[i];
    INode* current = g_storage->find_inode(current_inode);

    if (!current || current->type != NodeType::DIRECTORY) {
      return Result<uint32_t, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
    }

    // Search for child with matching name
    bool found = false;
    for (size_t j = 0; j < current->children.size(); j++) {
      uint32_t child_inode_num = current->children[j];
      INode* child = g_storage->find_inode(child_inode_num);
      if (child && child->name == name) {
        current_inode = child_inode_num;
        found = true;
        break;
      }
    }

    if (!found) {
      return Result<uint32_t, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
    }
  }

  return Result<uint32_t, ErrorCode>::ok(current_inode);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_open(const ou::string& path, uintptr_t flags) {
  auto inode_result = resolve_path(path);

  uint32_t inode_num = 0;
  if (inode_result.is_err()) {
    if (flags & OPEN_CREATE) {
      PathComponents components;
      split_path(path, components);
      if (components.parts.empty()) {
        return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
      }

      ou::string parent_path = "/";
      for (size_t i = 0; i < components.parts.size() - 1; i++) {
        parent_path.append(components.parts[i]);
        parent_path.append("/");
      }

      auto parent_result = resolve_path(parent_path);
      if (parent_result.is_err()) {
        return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
      }

      uint32_t parent_inode_num = parent_result.value();
      INode* parent = g_storage->find_inode(parent_inode_num);
      if (!parent || parent->type != NodeType::DIRECTORY) {
        return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
      }

      INode new_file;
      new_file.inode_num = g_storage->next_inode_num++;
      new_file.type = NodeType::FILE;
      new_file.name = components.parts[components.parts.size() - 1];
      new_file.parent_inode = parent_inode_num;
      new_file.created_time = 0;
      new_file.modified_time = 0;

      parent->children.push_back(new_file.inode_num);
      g_storage->inodes.push_back(static_cast<INode&&>(new_file));
      inode_num = new_file.inode_num;
    } else {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
    }
  } else {
    inode_num = inode_result.value();
    INode* inode = g_storage->find_inode(inode_num);

    if (inode && (flags & OPEN_TRUNCATE) && inode->type == NodeType::FILE) {
      inode->data.clear();
      inode->modified_time = 0;
    }
  }

  FileHandle* handle = g_storage->allocate_handle();
  if (!handle) {
    return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__TOO_MANY_OPEN_FILES);
  }

  handle->inode_num = inode_num;
  handle->flags = flags;

  return Result<uintptr_t, ErrorCode>::ok(handle->handle_id);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_read(uintptr_t handle_id, uintptr_t offset, uintptr_t length) {
  FileHandle* handle = g_storage->find_handle(handle_id);
  if (!handle) {
    return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
  }

  INode* inode = g_storage->find_inode(handle->inode_num);
  if (!inode || inode->type != NodeType::FILE) {
    return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
  }

  size_t file_size = inode->data.size();
  if (offset >= file_size) {
    return Result<uintptr_t, ErrorCode>::ok(0);
  }

  size_t available = file_size - offset;
  size_t bytes_to_read = (length < available) ? length : available;

  PageAddr comm = ou_get_comm_page();
  MPackWriter writer(comm.as_ptr(), OT_PAGE_SIZE);
  writer.bin(inode->data.data() + offset, bytes_to_read);

  return Result<uintptr_t, ErrorCode>::ok(bytes_to_read);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_write(uintptr_t handle_id, uintptr_t offset, const ou::vector<uint8_t>& data) {
  FileHandle* handle = g_storage->find_handle(handle_id);
  if (!handle) {
    return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
  }

  INode* inode = g_storage->find_inode(handle->inode_num);
  if (!inode || inode->type != NodeType::FILE) {
    return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
  }

  size_t length = data.size();
  size_t required_size = offset + length;
  if (inode->data.size() < required_size) {
    inode->data.resize(required_size, 0);
  }

  for (size_t i = 0; i < length; i++) {
    inode->data[offset + i] = data[i];
  }

  inode->modified_time = 0;

  return Result<uintptr_t, ErrorCode>::ok(length);
}

Result<bool, ErrorCode> FilesystemServer::handle_close(uintptr_t handle_id) {
  FileHandle* handle = g_storage->find_handle(handle_id);
  if (!handle) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
  }

  handle->is_open = false;
  return Result<bool, ErrorCode>::ok(true);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_read_all(const ou::string& path) {
  auto inode_result = resolve_path(path);
  if (inode_result.is_err()) {
    return Result<uintptr_t, ErrorCode>::err(inode_result.error());
  }

  INode* inode = g_storage->find_inode(inode_result.value());
  if (!inode || inode->type != NodeType::FILE) {
    return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
  }

  size_t file_size = inode->data.size();

  if (file_size > OT_PAGE_SIZE - 64) {
    return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__FILE_TOO_LARGE);
  }

  PageAddr comm = ou_get_comm_page();
  MPackWriter writer(comm.as_ptr(), OT_PAGE_SIZE);
  writer.bin(inode->data.data(), file_size);

  return Result<uintptr_t, ErrorCode>::ok(file_size);
}

Result<bool, ErrorCode> FilesystemServer::handle_write_all(const ou::string& path, const ou::vector<uint8_t>& data) {
  auto inode_result = resolve_path(path);
  uint32_t inode_num = 0;

  if (inode_result.is_ok()) {
    inode_num = inode_result.value();
    INode* inode = g_storage->find_inode(inode_num);
    if (!inode || inode->type != NodeType::FILE) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    inode->data.clear();
    for (size_t i = 0; i < data.size(); i++) {
      inode->data.push_back(data[i]);
    }
    inode->modified_time = 0;
  } else {
    PathComponents components;
    split_path(path, components);
    if (components.parts.empty()) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    ou::string parent_path = "/";
    for (size_t i = 0; i < components.parts.size() - 1; i++) {
      parent_path.append(components.parts[i]);
      parent_path.append("/");
    }

    auto parent_result = resolve_path(parent_path);
    if (parent_result.is_err()) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    uint32_t parent_inode_num = parent_result.value();
    INode* parent = g_storage->find_inode(parent_inode_num);
    if (!parent || parent->type != NodeType::DIRECTORY) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    INode new_file;
    new_file.inode_num = g_storage->next_inode_num++;
    new_file.type = NodeType::FILE;
    new_file.name = components.parts[components.parts.size() - 1];
    new_file.parent_inode = parent_inode_num;
    for (size_t i = 0; i < data.size(); i++) {
      new_file.data.push_back(data[i]);
    }
    new_file.created_time = 0;
    new_file.modified_time = 0;

    parent->children.push_back(new_file.inode_num);
    g_storage->inodes.push_back(static_cast<INode&&>(new_file));
  }

  return Result<bool, ErrorCode>::ok(true);
}

Result<bool, ErrorCode> FilesystemServer::handle_create_dir(const ou::string& path) {
  if (path.length() > MAX_PATH_LENGTH) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__PATH_TOO_LONG);
  }

  auto existing = resolve_path(path);
  if (existing.is_ok()) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__ALREADY_EXISTS);
  }

  PathComponents components;
  split_path(path, components);
  if (components.parts.empty()) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__ALREADY_EXISTS);
  }

  ou::string parent_path = "/";
  for (size_t i = 0; i < components.parts.size() - 1; i++) {
    parent_path.append(components.parts[i]);
    parent_path.append("/");
  }

  auto parent_result = resolve_path(parent_path);
  if (parent_result.is_err()) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
  }

  uint32_t parent_inode_num = parent_result.value();
  INode* parent = g_storage->find_inode(parent_inode_num);
  if (!parent || parent->type != NodeType::DIRECTORY) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
  }

  INode new_dir;
  new_dir.inode_num = g_storage->next_inode_num++;
  new_dir.type = NodeType::DIRECTORY;
  new_dir.name = components.parts[components.parts.size() - 1];
  new_dir.parent_inode = parent_inode_num;
  new_dir.created_time = 0;
  new_dir.modified_time = 0;

  parent->children.push_back(new_dir.inode_num);
  g_storage->inodes.push_back(static_cast<INode&&>(new_dir));

  return Result<bool, ErrorCode>::ok(true);
}

Result<bool, ErrorCode> FilesystemServer::handle_delete_file(const ou::string& path) {
  auto inode_result = resolve_path(path);
  if (inode_result.is_err()) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
  }

  uint32_t inode_num = inode_result.value();
  INode* inode = g_storage->find_inode(inode_num);
  if (!inode || inode->type != NodeType::FILE) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
  }

  INode* parent = g_storage->find_inode(inode->parent_inode);
  if (parent) {
    for (size_t i = 0; i < parent->children.size(); i++) {
      if (parent->children[i] == inode_num) {
        for (size_t j = i; j < parent->children.size() - 1; j++) {
          parent->children[j] = parent->children[j + 1];
        }
        parent->children.resize(parent->children.size() - 1);
        break;
      }
    }
  }

  inode->name.clear();
  inode->data.clear();

  return Result<bool, ErrorCode>::ok(true);
}

Result<bool, ErrorCode> FilesystemServer::handle_delete_dir(const ou::string& path) {
  auto inode_result = resolve_path(path);
  if (inode_result.is_err()) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__DIR_NOT_FOUND);
  }

  uint32_t inode_num = inode_result.value();
  INode* inode = g_storage->find_inode(inode_num);
  if (!inode || inode->type != NodeType::DIRECTORY) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__DIR_NOT_FOUND);
  }

  if (!inode->children.empty()) {
    return Result<bool, ErrorCode>::err(FILESYSTEM__NOT_EMPTY);
  }

  INode* parent = g_storage->find_inode(inode->parent_inode);
  if (parent) {
    for (size_t i = 0; i < parent->children.size(); i++) {
      if (parent->children[i] == inode_num) {
        for (size_t j = i; j < parent->children.size() - 1; j++) {
          parent->children[j] = parent->children[j + 1];
        }
        parent->children.resize(parent->children.size() - 1);
        break;
      }
    }
  }

  inode->name.clear();

  return Result<bool, ErrorCode>::ok(true);
}

void proc_filesystem(void) {
  // Initialize storage
  void *storage_page = ou_get_storage().as_ptr();
  g_storage = new (storage_page) FilesystemStorage();

  // Create root directory
  INode root;
  root.inode_num = 0;
  root.type = NodeType::DIRECTORY;
  root.name = "";
  root.parent_inode = 0;
  root.created_time = 0;
  root.modified_time = 0;
  g_storage->inodes.push_back(static_cast<INode&&>(root));
  g_storage->next_inode_num = 1;

  // Create and run server
  FilesystemServer server;
  server.run();
}
