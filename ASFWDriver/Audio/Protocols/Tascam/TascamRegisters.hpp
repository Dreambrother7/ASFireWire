// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ASFireWire Project
//
// TascamRegisters.hpp - Register plane for TASCAM FireWire series
// (FW-1884 / FW-1082 / FW-1804).
//
// Pure value codecs and ordered write sequences; no transport. The eventual
// IDeviceProtocol implementation issues these as async quadlet transactions
// at kAddrBase + offset (big-endian quadlets on the wire).
//
// Wire truth cross-validated with Linux sound/firewire/tascam:
//   - tascam.h:102-128        (address base and register offsets)
//   - tascam-stream.c:16-139  (clock register semantics and retry rule)
//   - tascam-stream.c:141-191 (channel enables, stream format option)
//   - tascam-stream.c:193-279 (session begin/finish write ordering)
//   - tascam-transaction.c:292-399 (MIDI messaging registration, LED)

#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace ASFW::Audio::Tascam {

inline constexpr uint64_t kAddrBase = 0xffff00000000ull;

enum class Reg : uint32_t {
    FirmwareRegister = 0x0000,
    FirmwareFpga = 0x0004,
    FirmwareArm = 0x0008,
    FirmwareHw = 0x000c,
    IsocTxCh = 0x0200,
    Unknown0204 = 0x0204,
    StartStreaming = 0x0208,
    IsocRxCh = 0x020c,
    IsocRxOn = 0x0210,
    TxPcmChannels = 0x0214,
    RxPcmChannels = 0x0218,
    MultiplexMode = 0x021c,
    IsocTxOn = 0x0220,
    ClockStatus = 0x0228,
    SetOption = 0x022c,
    MidiTxOn = 0x0300,
    MidiTxAddrHi = 0x0304,
    MidiTxAddrLo = 0x0308,
    LedPower = 0x0404,
    MidiRxQuad = 0x4000,
};

struct RegisterWrite {
    Reg reg;
    uint32_t value;
};

enum class ClockSource : uint32_t {
    Internal = 0,
    Word = 1,
    Spdif = 2,
    Adat = 3,
};

//==============================================================================
// Clock register (0x0228): status in the high 16 bits, config in the low 16
// (tascam-stream.c:11-12).
//==============================================================================

/// After a clock change the device reports an intermediate state with the
/// status half zeroed; poll until this holds (tascam-stream.c:22-39; Linux
/// retries 5x with 50ms sleeps).
[[nodiscard]] constexpr bool ClockStatusValid(uint32_t data) noexcept {
    return (data & 0xffff0000u) != 0;
}

/// Decode the sampling rate from bits [31:24]: base nibble 0x1=44.1k /
/// 0x2=48k, multiplier nibble 0x8=x2 (tascam-stream.c:97-123).
[[nodiscard]] constexpr std::optional<uint32_t> DecodeRate(
    uint32_t data) noexcept {
    const uint32_t field = (data >> 24) & 0xffu;
    uint32_t rate = 0;
    if ((field & 0x0fu) == 0x01u) {
        rate = 44100;
    } else if ((field & 0x0fu) == 0x02u) {
        rate = 48000;
    } else {
        return std::nullopt;
    }
    if ((field & 0xf0u) == 0x80u) {
        rate *= 2;
    } else if ((field & 0xf0u) != 0x00u) {
        return std::nullopt;
    }
    return rate;
}

/// Decode the clock source from bits [23:16], stored as enum+1
/// (tascam-stream.c:125-139).
[[nodiscard]] constexpr std::optional<ClockSource> DecodeClockSource(
    uint32_t data) noexcept {
    const uint32_t raw = (data & 0x00ff0000u) >> 16;
    if (raw < 1u || raw > 4u) {
        return std::nullopt;
    }
    return static_cast<ClockSource>(raw - 1u);
}

/// Clock configuration write plus its mandatory MultiplexMode follow-up.
struct ClockConfig {
    uint32_t clockStatusValue;    ///< Write to Reg::ClockStatus.
    uint32_t multiplexModeValue;  ///< Then write to Reg::MultiplexMode.
};

/// Build the clock write from the current register value, mirroring Linux
/// set_clock (tascam-stream.c:44-95): rate update preserves the source byte,
/// source update preserves the rate byte; the multiplex mode follow-up is
/// 0x1a at 2x rates and 0x0d at 1x.
[[nodiscard]] constexpr std::optional<ClockConfig> EncodeClockConfig(
    uint32_t currentData, uint32_t rate,
    std::optional<ClockSource> source) noexcept {
    uint32_t data = currentData & 0x0000ffffu;

    if (rate > 0) {
        data &= 0x000000ffu;
        if (rate % 44100u == 0) {
            data |= 0x00000100u;
            if (rate / 44100u == 2u) {
                data |= 0x00008000u;
            }
        } else if (rate % 48000u == 0) {
            data |= 0x00000200u;
            if (rate / 48000u == 2u) {
                data |= 0x00008000u;
            }
        } else {
            return std::nullopt;
        }
    }

    if (source.has_value()) {
        data &= 0x0000ff00u;
        data |= static_cast<uint32_t>(*source) + 1u;
    }

    const uint32_t multiplex = (data & 0x00008000u) ? 0x0000001au : 0x0000000du;
    return ClockConfig{data, multiplex};
}

//==============================================================================
// Data channel enables (tascam-stream.c:141-175)
//==============================================================================

[[nodiscard]] constexpr uint32_t ChannelEnableMask(uint32_t analogChannels,
                                                   bool hasAdat,
                                                   bool hasSpdif) noexcept {
    uint32_t mask = 0;
    for (uint32_t i = 0; i < analogChannels; ++i) {
        mask |= 1u << i;
    }
    if (hasAdat) {
        mask |= 0x0000ff00u;
    }
    if (hasSpdif) {
        mask |= 0x00030000u;
    }
    return mask;
}

/// Stream-format setup preceding a session (tascam-stream.c:177-191):
/// an option write of unknown purpose, then both channel-enable masks.
[[nodiscard]] constexpr std::array<RegisterWrite, 3> SetStreamFormatWrites(
    uint32_t captureMask, uint32_t playbackMask) noexcept {
    return {{
        {Reg::SetOption, 0x00200000u},
        {Reg::TxPcmChannels, captureMask},
        {Reg::RxPcmChannels, playbackMask},
    }};
}

//==============================================================================
// Session lifecycle (tascam-stream.c:193-279). Order is load-bearing.
//==============================================================================

[[nodiscard]] constexpr std::array<RegisterWrite, 7> BeginSessionWrites(
    uint32_t deviceTxIsoChannel, uint32_t deviceRxIsoChannel) noexcept {
    return {{
        {Reg::IsocTxCh, deviceTxIsoChannel},
        {Reg::Unknown0204, 0x00000002u},
        {Reg::IsocRxCh, deviceRxIsoChannel},
        {Reg::StartStreaming, 0x00000001u},
        {Reg::IsocRxOn, 0x00000001u},
        {Reg::SetOption, 0x00002000u},
        {Reg::IsocTxOn, 0x00000001u},  // Last: starts PCM multiplexing.
    }};
}

[[nodiscard]] constexpr std::array<RegisterWrite, 5> FinishSessionWrites() noexcept {
    return {{
        {Reg::StartStreaming, 0x00000000u},
        {Reg::IsocRxOn, 0x00000000u},
        {Reg::IsocTxCh, 0x00000000u},
        {Reg::Unknown0204, 0x00000000u},
        {Reg::IsocRxCh, 0x00000000u},
    }};
}

//==============================================================================
// MIDI messaging registration and LED (tascam-transaction.c:292-399).
// Bus reset clears these device registers; re-issue after every reset.
//==============================================================================

inline constexpr uint32_t kLedOn = 0x0001008eu;
inline constexpr uint32_t kLedOff = 0x0000008eu;

/// Program the host address the device writes incoming MIDI to, enable
/// messaging, and light the FireWire LED (tascam-transaction.c:333-368).
[[nodiscard]] constexpr std::array<RegisterWrite, 4> RegisterMidiMessagingWrites(
    uint16_t hostNodeId, uint64_t hostAddress) noexcept {
    return {{
        {Reg::MidiTxAddrHi,
         (static_cast<uint32_t>(hostNodeId) << 16) |
             static_cast<uint32_t>(hostAddress >> 32)},
        {Reg::MidiTxAddrLo, static_cast<uint32_t>(hostAddress)},
        {Reg::MidiTxOn, 0x00000001u},
        {Reg::LedPower, kLedOn},
    }};
}

[[nodiscard]] constexpr std::array<RegisterWrite, 4> UnregisterMidiMessagingWrites() noexcept {
    return {{
        {Reg::LedPower, kLedOff},
        {Reg::MidiTxOn, 0x00000000u},
        {Reg::MidiTxAddrHi, 0x00000000u},
        {Reg::MidiTxAddrLo, 0x00000000u},
    }};
}

/// Device-claimed host address region for incoming MIDI writes
/// (tascam-transaction.c:294-297).
inline constexpr uint64_t kMidiMessageRegionStart = 0xffffe0000000ull;
inline constexpr uint64_t kMidiMessageRegionEnd = 0xffffe000ffffull;

} // namespace ASFW::Audio::Tascam
