// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// MotuV2Protocol.hpp - Device protocol for MOTU protocol-v2 register devices (828mkII).
//
// Thin adapter: all wire encoding/decoding lives in the pure codecs of
// MotuV2Registers.hpp; this class owns only transport (async register IO against
// kAddrBase + offset) and the cached device state those reads produce.
//
// Streaming bring-up is deliberately NOT implemented here. MOTU playback must replay the
// capture stream's SPH timestamps (see docs/motu-828mk2-backend-design.md §6), which
// needs the shared IR->IT cadence conduit that is being built upstream; the base class
// returns kIOReturnUnsupported for those hooks until it lands.

#pragma once

#include "MotuV2Registers.hpp"
#include "../IDeviceProtocol.hpp"
#include "../../../Protocols/Ports/ProtocolRegisterIO.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>

namespace ASFW::Audio::Motu {

/// Device clock state as reported by the clock status register (0x0b14).
struct ClockStatus {
    uint32_t raw{0};
    uint32_t sampleRateHz{0};                ///< 0 when the rate index is unknown.
    std::optional<ClockSourceV2> source{};   ///< nullopt for reserved source codes.
};

class MotuV2Protocol final : public IDeviceProtocol {
public:
    using ClockStatusCallback = std::function<void(IOReturn, ClockStatus)>;
    using CompletionCallback = std::function<void(IOReturn)>;

    MotuV2Protocol(Protocols::Ports::FireWireBusOps& busOps,
                   Protocols::Ports::FireWireBusInfo& busInfo,
                   uint16_t nodeId,
                   uint32_t unitSwVersion);

    IOReturn Initialize() override;
    IOReturn Shutdown() override;
    const char* GetName() const override;

    void UpdateRuntimeContext(uint16_t nodeId,
                              Protocols::AVC::FCPTransport* transport) override;

    /// Read and decode the clock status register.
    void ReadClockStatus(ClockStatusCallback callback);

    /// Read-modify-write the sample rate, preserving the clock source and all other bits.
    /// Fails with kIOReturnUnsupported for a rate the device does not implement.
    void SetSampleRate(uint32_t rateHz, CompletionCallback callback);

    /// Last successfully decoded sample rate, or 0 before the first read completes.
    [[nodiscard]] uint32_t CachedSampleRateHz() const noexcept {
        return cachedSampleRateHz_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t UnitSwVersion() const noexcept { return unitSwVersion_; }

private:
    [[nodiscard]] static Async::FWAddress AddressOf(Reg reg) noexcept;

    Protocols::Ports::ProtocolRegisterIO io_;
    const uint32_t unitSwVersion_;
    std::atomic<uint32_t> cachedSampleRateHz_{0};
    bool initialized_{false};
};

} // namespace ASFW::Audio::Motu
