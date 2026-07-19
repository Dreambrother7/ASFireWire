// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// TascamBlockCodec.hpp - Encode/decode of TASCAM FireWire data blocks.
//
// TASCAM samples are plain 32-bit big-endian quadlets with 24 significant
// MSBs; the driver forwards the 32-bit word verbatim apart from byte order
// (cross-validated with Linux amdtp-tascam.c:35-127, which moves the ALSA
// S32 sample with cpu_to_be32/be32_to_cpu and constrains msbits to 24).

#pragma once

#include <DriverKit/IOLib.h>
#include "TascamBlockLayout.hpp"
#include <cstdint>
#include <span>

namespace ASFW::Encoding::Tascam {

/// Encode one host sample (signed, 24 significant MSBs of an int32) to a wire
/// quadlet. Matches Linux behavior: byte order conversion only, no masking.
[[nodiscard]] constexpr uint32_t EncodeSample(int32_t sample24MsbAligned) noexcept {
    return OSSwapHostToBigInt32(static_cast<uint32_t>(sample24MsbAligned));
}

[[nodiscard]] constexpr uint32_t EncodeSilence() noexcept {
    return 0u;
}

[[nodiscard]] constexpr int32_t DecodeSample(uint32_t wireQuadlet) noexcept {
    return static_cast<int32_t>(OSSwapBigToHostInt32(wireQuadlet));
}

/// Write one playback (host->device) data block: bare channel-ordered PCM
/// quadlets (amdtp-tascam.c:54-62). `block` must hold at least
/// PlaybackBlockQuadlets(samples.size()) quadlets.
inline void EncodePlaybackBlock(std::span<const int32_t> samples,
                                std::span<uint32_t> block) noexcept {
    for (size_t c = 0; c < samples.size(); ++c) {
        block[c] = EncodeSample(samples[c]);
    }
}

inline void EncodePlaybackSilence(uint32_t pcmChannels,
                                  std::span<uint32_t> block) noexcept {
    for (uint32_t c = 0; c < pcmChannels; ++c) {
        block[c] = EncodeSilence();
    }
}

/// Decoded view of one capture (device->host) data block.
struct CaptureBlockView {
    uint32_t eventCounter = 0;      ///< Host order (amdtp-tascam.c:141 uses it % 64).
    std::span<const uint32_t> pcm;  ///< Wire-order PCM quadlets (use DecodeSample).
    uint32_t statusQuadlet = 0;     ///< Host order; one cell of the state image.
};

/// Split one capture block into counter / PCM / status. `block` layout per
/// amdtp-tascam.c:84-96 (PCM at offset 1) and :143 (status is the last
/// quadlet). Returns an empty view if the block is too small.
[[nodiscard]] inline CaptureBlockView DecodeCaptureBlock(
    std::span<const uint32_t> block, uint32_t pcmChannels) noexcept {
    CaptureBlockView view{};
    if (block.size() < CaptureBlockQuadlets(pcmChannels)) {
        return view;
    }
    view.eventCounter = OSSwapBigToHostInt32(block[0]);
    view.pcm = block.subspan(kCaptureLeadingQuadlets, pcmChannels);
    view.statusQuadlet = OSSwapBigToHostInt32(block[block.size() - 1]);
    return view;
}

} // namespace ASFW::Encoding::Tascam
