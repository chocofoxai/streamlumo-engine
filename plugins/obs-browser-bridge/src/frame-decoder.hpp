/**
 * @file frame-decoder.hpp
 * @brief Base64 decoder for CEF browser frame data
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 StreamLumo
 *
 * ## Frame Format
 * 
 * Frames are BGRA (Blue, Green, Red, Alpha) pixel data encoded as base64.
 * Total decoded size = width * height * 4 bytes.
 * 
 * ## JSON Escaping
 * 
 * The base64 string may contain \/ escape sequences from JSON encoding.
 * This decoder handles the unescaping automatically.
 * 
 * ## Performance Note
 * 
 * Base64 over TCP is inefficient for high frame rates. Future versions
 * should use shared memory for frame transfer.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace browser_bridge {
namespace frame_decoder {

/**
 * Decodes base64-encoded BGRA frame data.
 * 
 * Handles JSON escape sequences (\/) before decoding.
 * 
 * @param base64 The base64-encoded string (may contain \/ escapes)
 * @param output Output vector for decoded BGRA bytes
 * @return true on success, false on decode error
 */
bool decodeBase64BGRA(const std::string &base64, std::vector<uint8_t> &output);

} // namespace frame_decoder
} // namespace browser_bridge
