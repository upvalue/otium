/**
 * FatFs disk I/O glue layer for Otium OS
 *
 * This file implements the diskio.h interface required by FatFs,
 * bridging it to our abstract Disk class.
 */

// Include FatFs headers from vendor directory
// NOTE: ff.h must be included before diskio.h as it defines the types used there
// clang-format off
extern "C" {
#include "ot/vendor/fatfs/ff.h"
#include "ot/vendor/fatfs/diskio.h"
}
// clang-format on

#include "ot/user/fs/disk.hpp"

// Global disk pointer - set by fatfs_set_disk() before mounting
static Disk *g_disk = nullptr;
static bool g_disk_initialized = false;

/**
 * Set the disk instance to use for FatFs operations.
 * Must be called before f_mount().
 */
void fatfs_set_disk(Disk *disk) {
  g_disk = disk;
  g_disk_initialized = false;
}

extern "C" {

/**
 * Initialize the disk drive.
 * Called by FatFs when mounting a volume.
 */
DSTATUS disk_initialize(BYTE pdrv) {
  if (pdrv != 0) {
    return STA_NOINIT; // Only drive 0 supported
  }

  if (g_disk == nullptr) {
    return STA_NOINIT;
  }

  g_disk_initialized = true;
  return 0; // Success
}

/**
 * Get disk status.
 */
DSTATUS disk_status(BYTE pdrv) {
  if (pdrv != 0) {
    return STA_NOINIT;
  }

  if (g_disk == nullptr || !g_disk_initialized) {
    return STA_NOINIT;
  }

  return 0; // Ready
}

/**
 * Read sectors from disk.
 */
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
  if (pdrv != 0 || g_disk == nullptr) {
    return RES_PARERR;
  }

  for (UINT i = 0; i < count; i++) {
    ErrorCode err = g_disk->read_sector(sector + i, buff + (i * DISK_SECTOR_SIZE));
    if (err != NONE) {
      return RES_ERROR;
    }
  }

  return RES_OK;
}

/**
 * Write sectors to disk.
 */
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
  if (pdrv != 0 || g_disk == nullptr) {
    return RES_PARERR;
  }

  for (UINT i = 0; i < count; i++) {
    ErrorCode err = g_disk->write_sector(sector + i, buff + (i * DISK_SECTOR_SIZE));
    if (err != NONE) {
      return RES_ERROR;
    }
  }

  return RES_OK;
}

/**
 * Disk I/O control.
 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  if (pdrv != 0 || g_disk == nullptr) {
    return RES_PARERR;
  }

  switch (cmd) {
  case CTRL_SYNC:
    // No buffering in our implementation, always synced
    return RES_OK;

  case GET_SECTOR_COUNT:
    *(LBA_t *)buff = (LBA_t)g_disk->sector_count();
    return RES_OK;

  case GET_SECTOR_SIZE:
    *(WORD *)buff = DISK_SECTOR_SIZE;
    return RES_OK;

  case GET_BLOCK_SIZE:
    // Erase block size in sectors (1 = unknown/not applicable)
    *(DWORD *)buff = 1;
    return RES_OK;

  default:
    return RES_PARERR;
  }
}

} // extern "C"
