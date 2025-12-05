#ifndef OT_USER_DISK_HPP
#define OT_USER_DISK_HPP

#include "ot/lib/error-codes.hpp"
#include <stdint.h>

#define DISK_SECTOR_SIZE 512

/**
 * Abstract disk interface for filesystem backends.
 * Provides sector-based read/write operations.
 */
class Disk {
public:
  virtual ~Disk() = default;

  /**
   * Read a single sector into buffer.
   * @param sector Sector number to read
   * @param buf Buffer to read into (must be DISK_SECTOR_SIZE bytes)
   * @return true on success, false on failure
   */
  virtual bool read_sector(uint64_t sector, uint8_t *buf) = 0;

  /**
   * Write a single sector from buffer.
   * @param sector Sector number to write
   * @param buf Buffer to write from (must be DISK_SECTOR_SIZE bytes)
   * @return true on success, false on failure
   */
  virtual bool write_sector(uint64_t sector, const uint8_t *buf) = 0;

  /**
   * Get disk capacity in sectors.
   * @return Number of sectors available on disk
   */
  virtual uint64_t sector_count() const = 0;
};

#endif
