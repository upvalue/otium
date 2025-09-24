/**
  * This is a one file implementation of some of the Pico SDK / datasheet functionality
  */

// APB registers. section 2.2.4, pg 31 
#define IO_BANK0_BASE 0x40028000
#define IO_QSPI_BASE  0x40030000

// Bootrom codes. section 5.4.3, pg 378 

// Success codes
#define BOOTROM_OK                              0    // The function executed successfully

// General error codes  
#define BOOTROM_ERROR_NOT_PERMITTED            -4    // The operation was disallowed by a security constraint
#define BOOTROM_ERROR_INVALID_ARG              -5    // One or more parameters passed to the function is outside the range of supported values

// Address and alignment errors
#define BOOTROM_ERROR_INVALID_ADDRESS         -10    // An address argument was out-of-bounds or was determined to be an address that the caller may not access
#define BOOTROM_ERROR_BAD_ALIGNMENT           -11    // An address passed to the function was not correctly aligned

// State and buffer errors
#define BOOTROM_ERROR_INVALID_STATE           -12    // Something happened or failed to happen in the past, and consequently the request cannot currently be serviced
#define BOOTROM_ERROR_BUFFER_TOO_SMALL        -13    // A user-allocated buffer was too small to hold the result or working state of the function
#define BOOTROM_ERROR_PRECONDITION_NOT_MET    -14    // The call failed because another bootrom function must be called first

// Data errors
#define BOOTROM_ERROR_MODIFIED_DATA           -15    // Cached data was determined to be inconsistent with the full version of the data it was copied from
#define BOOTROM_ERROR_INVALID_DATA            -16    // The contents of a data structure are invalid
#define BOOTROM_ERROR_NOT_FOUND               -17    // An attempt was made to access something that does not exist; or, a search failed

// Modification and lock errors
#define BOOTROM_ERROR_UNSUPPORTED_MODIFICATION -18   // Modification is impossible based on current state. This might occur, for example, when attempting to clear an OTP bit
#define BOOTROM_ERROR_LOCK_REQUIRED           -19    // A required lock is not owned

#ifdef PICO_IMPL

#endif


