// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// TascamBlockLayout.hpp - Data-block geometry for TASCAM FireWire series
// (FW-1884 / FW-1082 / FW-1804).
//
// Wire truth cross-validated with Linux sound/firewire/tascam:
//   - amdtp-tascam.c:18-33  (capture blocks carry 2 extra data channels)
//   - amdtp-tascam.c:84-96  (leading event-counter quadlet, PCM at offset 1)
//   - amdtp-tascam.c:129-176 (trailing status quadlet, 64-entry state image)
//   - tascam-stream.c:303-334 (PCM channel count = analog + ADAT(8) + S/PDIF(2))

#pragma once

#include <cstdint>

namespace ASFW::Encoding::Tascam {

/// Model capability description, mirroring the Linux spec table (tascam.c:15-43).
struct ModelSpec {
    uint32_t captureAnalogChannels;
    uint32_t playbackAnalogChannels;
    bool hasAdat;
    bool hasSpdif;
};

inline constexpr ModelSpec kFw1884Spec{8, 8, true, true};
inline constexpr ModelSpec kFw1082Spec{8, 2, false, true};
inline constexpr ModelSpec kFw1804Spec{8, 2, true, true};

inline constexpr uint32_t kAdatChannels = 8;
inline constexpr uint32_t kSpdifChannels = 2;

/// Capture (device->host) blocks: one leading event-counter quadlet and one
/// trailing status quadlet around the PCM quadlets.
inline constexpr uint32_t kCaptureLeadingQuadlets = 1;
inline constexpr uint32_t kCaptureTrailingQuadlets = 1;

/// Rotating control-surface state image carried by capture status quadlets
/// (amdtp-tascam.c:141, SNDRV_FIREWIRE_TASCAM_STATE_COUNT).
inline constexpr uint32_t kStateImageEntries = 64;

[[nodiscard]] constexpr uint32_t CapturePcmChannels(const ModelSpec& spec) noexcept {
    return spec.captureAnalogChannels + (spec.hasAdat ? kAdatChannels : 0u) +
           (spec.hasSpdif ? kSpdifChannels : 0u);
}

[[nodiscard]] constexpr uint32_t PlaybackPcmChannels(const ModelSpec& spec) noexcept {
    return spec.playbackAnalogChannels + (spec.hasAdat ? kAdatChannels : 0u) +
           (spec.hasSpdif ? kSpdifChannels : 0u);
}

/// Playback blocks are bare PCM quadlets, one per channel (amdtp-tascam.c:35-63).
[[nodiscard]] constexpr uint32_t PlaybackBlockQuadlets(uint32_t pcmChannels) noexcept {
    return pcmChannels;
}

/// Capture blocks add the event-counter and status quadlets (amdtp-tascam.c:28-32).
[[nodiscard]] constexpr uint32_t CaptureBlockQuadlets(uint32_t pcmChannels) noexcept {
    return kCaptureLeadingQuadlets + pcmChannels + kCaptureTrailingQuadlets;
}

} // namespace ASFW::Encoding::Tascam
