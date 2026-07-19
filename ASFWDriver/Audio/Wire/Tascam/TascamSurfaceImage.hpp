// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// TascamSurfaceImage.hpp - Rotating control-surface state image fed by the
// status quadlet of every TASCAM capture data block.
//
// Each capture frame carries one cell of a 64-entry image addressed by
// (event counter % 64). Faders, buttons, knobs and meters accumulate into the
// image; consumers watch for masked changes on the notified index range.
// Cross-validated with Linux amdtp-tascam.c:129-176. Index->control semantics
// live in snd-firewire-ctl-services (tascam runtime) and are intentionally
// not encoded here.

#pragma once

#include "TascamBlockLayout.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace ASFW::Encoding::Tascam {

struct SurfaceChange {
    uint32_t index = 0;
    uint32_t before = 0;  ///< Host order.
    uint32_t after = 0;   ///< Host order.
};

class SurfaceImage {
public:
    /// Indices outside [5, 15] update the image but never notify
    /// (amdtp-tascam.c:145: `index > 4 && index < 16`).
    static constexpr uint32_t kFirstNotifiedIndex = 5;
    static constexpr uint32_t kLastNotifiedIndex = 15;

    /// Bits ignored when diffing an index; filters meter/noise fields
    /// (amdtp-tascam.c:147-155).
    [[nodiscard]] static constexpr uint32_t NoiseMask(uint32_t index) noexcept {
        switch (index) {
        case 5:
        case 6:
            return ~0x0000ffffu;
        case 8:
            return ~0x000f0f00u;
        default:
            return ~0x00000000u;
        }
    }

    /// Fold one capture frame's status cell into the image. Returns a change
    /// record when a notified index differs under its noise mask.
    std::optional<SurfaceChange> Apply(uint32_t eventCounter,
                                       uint32_t statusQuadlet) noexcept {
        const uint32_t index = eventCounter % kStateImageEntries;
        const uint32_t before = state_[index];
        state_[index] = statusQuadlet;

        if (index < kFirstNotifiedIndex || index > kLastNotifiedIndex) {
            return std::nullopt;
        }
        if (((before ^ statusQuadlet) & NoiseMask(index)) == 0) {
            return std::nullopt;
        }
        return SurfaceChange{index, before, statusQuadlet};
    }

    [[nodiscard]] uint32_t Value(uint32_t index) const noexcept {
        return state_[index % kStateImageEntries];
    }

    void Reset() noexcept { state_.fill(0u); }

private:
    std::array<uint32_t, kStateImageEntries> state_{};
};

} // namespace ASFW::Encoding::Tascam
