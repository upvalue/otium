#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/filesystem/types.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"

using namespace filesystem;

// Filesystem server implementation with instance state
struct FilesystemServer : FilesystemServerBase {
  FilesystemStorage *storage;

  FilesystemServer() : storage(nullptr) {}

private:
  // Resolve path to inode number (private helper)
  Result<uint32_t, ErrorCode> resolve_path(const ou::string &path) {
    if (path.length() > MAX_PATH_LENGTH) {
      return Result<uint32_t, ErrorCode>::err(FILESYSTEM__PATH_TOO_LONG);
    }

    PathComponents components;
    split_path(path, components);

    // Start from root
    uint32_t current_inode = 0;

    // Traverse path components
    for (size_t i = 0; i < components.parts.size(); i++) {
      const ou::string &name = components.parts[i];
      INode *current = storage->find_inode(current_inode);

      if (!current || current->type != NodeType::DIRECTORY) {
        return Result<uint32_t, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
      }

      // Search for child with matching name
      bool found = false;
      for (size_t j = 0; j < current->children.size(); j++) {
        uint32_t child_inode_num = current->children[j];
        INode *child = storage->find_inode(child_inode_num);
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

public:
  Result<FileHandleId, ErrorCode> handle_open(const ou::string &path, uintptr_t flags) override {
    auto inode_result = resolve_path(path);

    uint32_t inode_num = 0;
    if (inode_result.is_err()) {
      if (flags & OPEN_CREATE) {
        PathComponents components;
        split_path(path, components);
        if (components.parts.empty()) {
          return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
        }

        ou::string parent_path = "/";
        for (size_t i = 0; i < components.parts.size() - 1; i++) {
          parent_path.append(components.parts[i]);
          parent_path.append("/");
        }

        auto parent_result = resolve_path(parent_path);
        if (parent_result.is_err()) {
          return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
        }

        uint32_t parent_inode_num = parent_result.value();
        INode *parent = storage->find_inode(parent_inode_num);
        if (!parent || parent->type != NodeType::DIRECTORY) {
          return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
        }

        INode new_file;
        new_file.inode_num = storage->next_inode_num++;
        new_file.type = NodeType::FILE;
        new_file.name = components.parts[components.parts.size() - 1];
        new_file.parent_inode = parent_inode_num;
        new_file.created_time = 0;
        new_file.modified_time = 0;

        parent->children.push_back(new_file.inode_num);
        storage->inodes.push_back(static_cast<INode &&>(new_file));
        inode_num = new_file.inode_num;
      } else {
        return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
      }
    } else {
      inode_num = inode_result.value();
      INode *inode = storage->find_inode(inode_num);

      if (inode && (flags & OPEN_TRUNCATE) && inode->type == NodeType::FILE) {
        inode->data.clear();
        inode->modified_time = 0;
      }
    }

    FileHandle *handle = storage->allocate_handle();
    if (!handle) {
      return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__TOO_MANY_OPEN_FILES);
    }

    handle->inode_num = inode_num;
    handle->flags = flags;

    return Result<FileHandleId, ErrorCode>::ok(FileHandleId(handle->handle_id));
  }

  Result<uintptr_t, ErrorCode> handle_read(FileHandleId handle_id, uintptr_t offset, uintptr_t length) override {
    FileHandle *handle = storage->find_handle(handle_id.raw());
    if (!handle) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    INode *inode = storage->find_inode(handle->inode_num);
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

  Result<uintptr_t, ErrorCode> handle_write(FileHandleId handle_id, uintptr_t offset,
                                            const StringView &data) override {
    FileHandle *handle = storage->find_handle(handle_id.raw());
    if (!handle) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    INode *inode = storage->find_inode(handle->inode_num);
    if (!inode || inode->type != NodeType::FILE) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    size_t length = data.len;
    size_t required_size = offset + length;
    if (inode->data.size() < required_size) {
      inode->data.resize(required_size, 0);
    }

    for (size_t i = 0; i < length; i++) {
      inode->data[offset + i] = static_cast<uint8_t>(data.ptr[i]);
    }

    inode->modified_time = 0;

    return Result<uintptr_t, ErrorCode>::ok(length);
  }

  Result<bool, ErrorCode> handle_close(FileHandleId handle_id) override {
    FileHandle *handle = storage->find_handle(handle_id.raw());
    if (!handle) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    handle->is_open = false;
    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_create_file(const ou::string &path) override {
    if (path.length() > MAX_PATH_LENGTH) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__PATH_TOO_LONG);
    }

    // Check if file already exists
    auto existing = resolve_path(path);
    if (existing.is_ok()) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__ALREADY_EXISTS);
    }

    // Split path and verify parent exists
    PathComponents components;
    split_path(path, components);
    if (components.parts.empty()) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
    }

    // Build parent path
    ou::string parent_path = "/";
    for (size_t i = 0; i < components.parts.size() - 1; i++) {
      parent_path.append(components.parts[i]);
      parent_path.append("/");
    }

    // Verify parent exists and is a directory
    auto parent_result = resolve_path(parent_path);
    if (parent_result.is_err()) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
    }

    uint32_t parent_inode_num = parent_result.value();
    INode *parent = storage->find_inode(parent_inode_num);
    if (!parent || parent->type != NodeType::DIRECTORY) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
    }

    // Create new file inode
    INode new_file;
    new_file.inode_num = storage->next_inode_num++;
    new_file.type = NodeType::FILE;
    new_file.name = components.parts[components.parts.size() - 1];
    new_file.parent_inode = parent_inode_num;
    new_file.created_time = 0;
    new_file.modified_time = 0;

    // Add to parent's children and storage
    parent->children.push_back(new_file.inode_num);
    storage->inodes.push_back(static_cast<INode &&>(new_file));

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_create_dir(const ou::string &path) override {
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
    INode *parent = storage->find_inode(parent_inode_num);
    if (!parent || parent->type != NodeType::DIRECTORY) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__PARENT_NOT_FOUND);
    }

    INode new_dir;
    new_dir.inode_num = storage->next_inode_num++;
    new_dir.type = NodeType::DIRECTORY;
    new_dir.name = components.parts[components.parts.size() - 1];
    new_dir.parent_inode = parent_inode_num;
    new_dir.created_time = 0;
    new_dir.modified_time = 0;

    parent->children.push_back(new_dir.inode_num);
    storage->inodes.push_back(static_cast<INode &&>(new_dir));

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_delete_file(const ou::string &path) override {
    auto inode_result = resolve_path(path);
    if (inode_result.is_err()) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
    }

    uint32_t inode_num = inode_result.value();
    INode *inode = storage->find_inode(inode_num);
    if (!inode || inode->type != NodeType::FILE) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
    }

    INode *parent = storage->find_inode(inode->parent_inode);
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

  Result<bool, ErrorCode> handle_delete_dir(const ou::string &path) override {
    auto inode_result = resolve_path(path);
    if (inode_result.is_err()) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__DIR_NOT_FOUND);
    }

    uint32_t inode_num = inode_result.value();
    INode *inode = storage->find_inode(inode_num);
    if (!inode || inode->type != NodeType::DIRECTORY) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__DIR_NOT_FOUND);
    }

    if (!inode->children.empty()) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__NOT_EMPTY);
    }

    INode *parent = storage->find_inode(inode->parent_inode);
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
};

void proc_filesystem(void) {
  // Initialize storage
  void *storage_page = ou_get_storage().as_ptr();
  FilesystemStorage *fs_storage = new (storage_page) FilesystemStorage();

  // Create root directory
  INode root;
  root.inode_num = 0;
  root.type = NodeType::DIRECTORY;
  root.name = "";
  root.parent_inode = 0;
  root.created_time = 0;
  root.modified_time = 0;
  fs_storage->inodes.push_back(static_cast<INode &&>(root));
  fs_storage->next_inode_num = 1;

  // Create server and set storage pointer
  FilesystemServer server;
  server.storage = fs_storage;

  // Run server
  server.run();
}
