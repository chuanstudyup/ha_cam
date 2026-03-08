/*
 * This file handles the embedding of HTML resources into the binary.
 * The actual HTML file (index.html) is embedded as a binary resource
 * during the build process using EMBED_FILES in CMakeLists.txt.
 */

#include "esp_log.h"
#include "WebServer.h"

// The actual binary data is embedded during compilation
// through the CMakeLists.txt EMBED_FILES directive
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

// This file is needed for the build system to properly
// track dependencies and ensure the HTML is embedded
