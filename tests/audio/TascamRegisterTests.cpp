// TascamRegisterTests.cpp
// ASFW - TASCAM register-plane codec tests
//
// Golden values cross-validated with Linux sound/firewire/tascam
// (tascam-stream.c, tascam-transaction.c, tascam.h).

#include <gtest/gtest.h>
#include "Audio/Protocols/Tascam/TascamRegisters.hpp"

using namespace ASFW::Audio::Tascam;

//==============================================================================
// Clock register decode (tascam-stream.c:97-139)
//==============================================================================

TEST(TascamClockTests, DetectsIntermediateState) {
    EXPECT_FALSE(ClockStatusValid(0x00000103u));  // status half zero
    EXPECT_TRUE(ClockStatusValid(0x01010103u));
}

TEST(TascamClockTests, DecodesBaseRates) {
    EXPECT_EQ(DecodeRate(0x01000000u).value(), 44100u);
    EXPECT_EQ(DecodeRate(0x02000000u).value(), 48000u);
}

TEST(TascamClockTests, DecodesDoubledRates) {
    EXPECT_EQ(DecodeRate(0x81000000u).value(), 88200u);
    EXPECT_EQ(DecodeRate(0x82000000u).value(), 96000u);
}

TEST(TascamClockTests, RejectsUnknownRateFields) {
    EXPECT_FALSE(DecodeRate(0x03000000u).has_value());  // bad base nibble
    EXPECT_FALSE(DecodeRate(0x41000000u).has_value());  // bad multiplier
}

TEST(TascamClockTests, DecodesClockSourceAsEnumPlusOne) {
    EXPECT_EQ(DecodeClockSource(0x00010000u).value(), ClockSource::Internal);
    EXPECT_EQ(DecodeClockSource(0x00020000u).value(), ClockSource::Word);
    EXPECT_EQ(DecodeClockSource(0x00030000u).value(), ClockSource::Spdif);
    EXPECT_EQ(DecodeClockSource(0x00040000u).value(), ClockSource::Adat);
    EXPECT_FALSE(DecodeClockSource(0x00000000u).has_value());
    EXPECT_FALSE(DecodeClockSource(0x00050000u).has_value());
}

//==============================================================================
// Clock register encode (tascam-stream.c:44-95)
//==============================================================================

TEST(TascamClockTests, RateChangePreservesSourceByte) {
    // Current config: 48k internal (0x0201). Request 44.1k, keep source.
    const auto cfg = EncodeClockConfig(0xABCD0201u, 44100u, std::nullopt);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->clockStatusValue, 0x00000101u);
    EXPECT_EQ(cfg->multiplexModeValue, 0x0000000du);  // 1x rate
}

TEST(TascamClockTests, DoubledRateSetsMultiplierAndMultiplexMode) {
    const auto cfg = EncodeClockConfig(0x00000201u, 96000u, std::nullopt);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->clockStatusValue, 0x00008201u);    // base 48k + x2, source kept
    EXPECT_EQ(cfg->multiplexModeValue, 0x0000001au);  // 2x rate
}

TEST(TascamClockTests, SourceChangePreservesRateByte) {
    // Rate 0 = leave rate alone (tascam-stream.c:56); set ADAT source.
    const auto cfg = EncodeClockConfig(0x00000201u, 0u, ClockSource::Adat);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->clockStatusValue, 0x00000204u);  // rate byte kept, source = 3+1
}

TEST(TascamClockTests, RateAndSourceTogether) {
    const auto cfg = EncodeClockConfig(0x00000000u, 88200u, ClockSource::Word);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->clockStatusValue, 0x00008102u);  // 44.1 base + x2 + word(1)+1
    EXPECT_EQ(cfg->multiplexModeValue, 0x0000001au);
}

TEST(TascamClockTests, RejectsUnsupportedRate) {
    EXPECT_FALSE(EncodeClockConfig(0u, 32000u, std::nullopt).has_value());
}

//==============================================================================
// Channel enables (tascam-stream.c:141-175)
//==============================================================================

TEST(TascamChannelTests, Fw1884FullMask) {
    // 8 analog + ADAT (0xff00) + SPDIF (0x30000) = 0x3ffff.
    EXPECT_EQ(ChannelEnableMask(8u, true, true), 0x0003ffffu);
}

TEST(TascamChannelTests, Fw1082PlaybackMask) {
    // 2 analog, no ADAT, SPDIF.
    EXPECT_EQ(ChannelEnableMask(2u, false, true), 0x00030003u);
}

TEST(TascamChannelTests, StreamFormatWriteSequence) {
    const auto writes = SetStreamFormatWrites(0x0003ffffu, 0x0003ffffu);
    EXPECT_EQ(writes[0].reg, Reg::SetOption);
    EXPECT_EQ(writes[0].value, 0x00200000u);
    EXPECT_EQ(writes[1].reg, Reg::TxPcmChannels);
    EXPECT_EQ(writes[2].reg, Reg::RxPcmChannels);
}

//==============================================================================
// Session sequences (tascam-stream.c:193-279) - exact order is load-bearing
//==============================================================================

TEST(TascamSessionTests, BeginSessionOrderMatchesReference) {
    const auto writes = BeginSessionWrites(3u, 7u);

    ASSERT_EQ(writes.size(), 7u);
    EXPECT_EQ(writes[0].reg, Reg::IsocTxCh);
    EXPECT_EQ(writes[0].value, 3u);
    EXPECT_EQ(writes[1].reg, Reg::Unknown0204);
    EXPECT_EQ(writes[1].value, 0x00000002u);
    EXPECT_EQ(writes[2].reg, Reg::IsocRxCh);
    EXPECT_EQ(writes[2].value, 7u);
    EXPECT_EQ(writes[3].reg, Reg::StartStreaming);
    EXPECT_EQ(writes[4].reg, Reg::IsocRxOn);
    EXPECT_EQ(writes[5].reg, Reg::SetOption);
    EXPECT_EQ(writes[5].value, 0x00002000u);
    // ISOC_TX_ON must be last: it starts PCM multiplexing.
    EXPECT_EQ(writes[6].reg, Reg::IsocTxOn);
    EXPECT_EQ(writes[6].value, 0x00000001u);
}

TEST(TascamSessionTests, FinishSessionStopsBeforeUnregistering) {
    const auto writes = FinishSessionWrites();

    ASSERT_EQ(writes.size(), 5u);
    EXPECT_EQ(writes[0].reg, Reg::StartStreaming);
    EXPECT_EQ(writes[0].value, 0u);
    EXPECT_EQ(writes[1].reg, Reg::IsocRxOn);
    EXPECT_EQ(writes[2].reg, Reg::IsocTxCh);
    EXPECT_EQ(writes[3].reg, Reg::Unknown0204);
    EXPECT_EQ(writes[4].reg, Reg::IsocRxCh);
}

//==============================================================================
// MIDI messaging + LED (tascam-transaction.c:292-399)
//==============================================================================

TEST(TascamMidiTests, RegistrationEncodesNodeAndAddress) {
    // Host node 0xffc1, handler at 0xffffe0000010.
    const auto writes = RegisterMidiMessagingWrites(0xffc1u, 0xffffe0000010ull);

    EXPECT_EQ(writes[0].reg, Reg::MidiTxAddrHi);
    EXPECT_EQ(writes[0].value, (0xffc1u << 16) | 0x0000ffffu);
    EXPECT_EQ(writes[1].reg, Reg::MidiTxAddrLo);
    EXPECT_EQ(writes[1].value, 0xe0000010u);
    EXPECT_EQ(writes[2].reg, Reg::MidiTxOn);
    EXPECT_EQ(writes[2].value, 1u);
    EXPECT_EQ(writes[3].reg, Reg::LedPower);
    EXPECT_EQ(writes[3].value, kLedOn);
}

TEST(TascamMidiTests, UnregistrationTurnsOffLedFirst) {
    const auto writes = UnregisterMidiMessagingWrites();
    EXPECT_EQ(writes[0].reg, Reg::LedPower);
    EXPECT_EQ(writes[0].value, kLedOff);
    EXPECT_EQ(writes[1].reg, Reg::MidiTxOn);
    EXPECT_EQ(writes[1].value, 0u);
}

TEST(TascamMidiTests, MessageRegionMatchesReference) {
    EXPECT_EQ(kMidiMessageRegionStart, 0xffffe0000000ull);
    EXPECT_EQ(kMidiMessageRegionEnd, 0xffffe000ffffull);
}
