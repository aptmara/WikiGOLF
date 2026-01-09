/* General ASSIMP configuration settings */

#ifndef ASSIMP_CONFIG_H_INC
#define ASSIMP_CONFIG_H_INC

// ------------------------------------------------------------------------------
/** @file config.h
 *  @brief General ASSIMP configuration settings
 *
 *  This file can be used to control some of ASSIMP's default settings.
 *  When you're building the library, you can copy this file to "config.h"
 *  and specify your own settings. When you're just using the library, you
 *  can #include this file and #define the settings you want to change in
 *  your project settings.
 */
// ------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
/** @brief Set to 1 to enable logging.
 *
 *  If logging is enabled, all log messages are routed to a user-provided
 *  LogStream. If no LogStream is provided, the DefaultLogger is used.
 */
#define ASSIMP_BUILD_DEBUG_LOGGING 1

// ------------------------------------------------------------------------------
/** @brief Set to 1 to disable all logging.
 *
 *  This is the default for release builds.
 */
#ifndef ASSIMP_BUILD_DEBUG_LOGGING
#   define ASSIMP_BUILD_NO_LOGGING 1
#endif

// ------------------------------------------------------------------------------
/** @brief Set to 1 to enable verbose logging.
 *
 *  Verbose logging includes debug information such as the memory consumption
 *  of the imported data.
 */
#define ASSIMP_BUILD_VERBOSE_LOGGING 1

// ------------------------------------------------------------------------------
/** @brief Set to 1 to disable all code related to importing.
 *
 *  This is useful if you're going to use the library primarily for
 *  exporting. This can reduce the size of the library significantly.
 */
#define ASSIMP_BUILD_NO_IMPORTER 0

// ------------------------------------------------------------------------------
/** @brief Set to 1 to disable all code related to exporting.
 *
 *  This is useful if you're going to use the library primarily for
 *  importing. This can reduce the size of the library significantly.
 */
#define ASSIMP_BUILD_NO_EXPORTER 1

// ------------------------------------------------------------------------------
/** @brief Set to 1 to enable multithreading.
 *
 *  If multithreading is enabled, some importers can use multiple threads
 *  to speed up the import process. This is the default for release builds.
 */
#define ASSIMP_BUILD_WITH_THREADING 1

#endif // ASSIMP_CONFIG_H_INC
