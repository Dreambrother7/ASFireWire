// TascamBlockCodecTests.cpp
// ASFW - TASCAM data-block layout, codec, and surface-image tests
//
// Golden values cross-validated with Linux sound/firewire/tascam
// (amdtp-tascam.c, tascam-stream.c, tascam.c spec table).

#include <gtest/gtest.h>
#include "Audio/Wire/Tascam/TascamBlockLayout.hpp"
#include "Audio/Wire/Tascam/TascamBlockCodec.hpp"
#include "Audio/Wire/Tascam/TascamSurfaceImage.hpp"
#include <array>
#include <vector>

using namespace ASFW::Encoding::Tascam;

//==============================================================================
// Block geometry (tascam.c:15-43 spec table + tascam-stream.c:303-334)
//==============================================================================

TEST(TascamBlockLayoutTests, Fw1884ChannelCounts) {
    EXPECT_EQ(CapturePcmChannels(kFw1884Spec), 18u);   // 8 analog + 8 ADAT + 2 SPDIF
    EXPECT_EQ(PlaybackPcmChannels(kFw1884Spec), 18u);
    EXPECT_EQ(CaptureBlockQuadlets(18u), 20u);         // counter + 18 PCM + status
    EXPECT_EQ(PlaybackBlockQuadlets(18u), 18u);
}

TEST(TascamBlockLayoutTests, Fw1082ChannelCounts) {
    EXPECT_EQ(CapturePcmChannels(kFw1082Spec), 10u);   // 8 analog + 2 SPDIF, no ADAT
    EXPECT_EQ(PlaybackPcmChannels(kFw1082Spec), 4u);   // 2 analog + 2 SPDIF
}

TEST(TascamBlockLayoutTests, Fw1804ChannelCounts) {
    EXPECT_EQ(CapturePcmChannels(kFw1804Spec), 18u);
    EXPECT_EQ(PlaybackPcmChannels(kFw1804Spec), 12u);  // 2 analog + 8 ADAT + 2 SPDIF
}

//==============================================================================
// Sample codec (amdtp-tascam.c:35-127 - byte order only, 24 MSBs significant)
//==============================================================================

TEST(TascamBlockCodecTests, EncodesSampleBigEndian) {
    // Host 0x12345600 (24-bit sample 0x123456 MSB-aligned) -> wire bytes
    // 12 34 56 00, i.e. host-LE representation 0x00563412.
    EXPECT_EQ(EncodeSample(0x12345600), 0x00563412u);
}

TEST(TascamBlockCodecTests, EncodesNegativeSampleVerbatim) {
    // Linux does not mask the low byte; neither do we.
    const auto sample = static_cast<int32_t>(0xFEDCBA00);
    EXPECT_EQ(EncodeSample(sample), 0x00BADCFEu);
}

TEST(TascamBlockCodecTests, SampleRoundTrips) {
    const int32_t samples[] = {0, 0x7FFFFF00, static_cast<int32_t>(0x80000000),
                               0x00000100, static_cast<int32_t>(0xFFFFFF00)};
    for (int32_t s : samples) {
        EXPECT_EQ(DecodeSample(EncodeSample(s)), s);
    }
}

TEST(TascamBlockCodecTests, EncodesPlaybackBlockChannelOrdered) {
    const std::array<int32_t, 4> frame{0x11111100, 0x22222200, 0x33333300,
                                       static_cast<int32_t>(0xCAFEBA00)};
    std::array<uint32_t, 4> block{};
    EncodePlaybackBlock(frame, block);

    for (size_t c = 0; c < frame.size(); ++c) {
        EXPECT_EQ(block[c], EncodeSample(frame[c])) << "channel " << c;
    }
}

TEST(TascamBlockCodecTests, EncodesPlaybackSilenceAsZeroQuadlets) {
    std::array<uint32_t, 18> block;
    block.fill(0xDEADBEEFu);
    EncodePlaybackSilence(18u, block);
    for (uint32_t q : block) {
        EXPECT_EQ(q, 0u);
    }
}

//==============================================================================
// Capture block decode (amdtp-tascam.c:65-96,129-176)
//==============================================================================

namespace {

// Build a golden FW-1884 capture block: counter, 18 PCM quadlets, status.
std::vector<uint32_t> MakeCaptureBlock(uint32_t counter, uint32_t status) {
    std::vector<uint32_t> block;
    block.push_back(OSSwapHostToBigInt32(counter));
    for (uint32_t c = 0; c < 18; ++c) {
        block.push_back(EncodeSample(static_cast<int32_t>((c + 1) << 8)));
    }
    block.push_back(OSSwapHostToBigInt32(status));
    return block;
}

} // namespace

TEST(TascamBlockCodecTests, DecodesCaptureBlockFields) {
    const auto block = MakeCaptureBlock(0x00000123u, 0xA5A5A5A5u);
    ASSERT_EQ(block.size(), CaptureBlockQuadlets(18u));

    const auto view = DecodeCaptureBlock(block, 18u);
    EXPECT_EQ(view.eventCounter, 0x00000123u);
    ASSERT_EQ(view.pcm.size(), 18u);
    EXPECT_EQ(DecodeSample(view.pcm[0]), 0x00000100);
    EXPECT_EQ(DecodeSample(view.pcm[17]), 0x00001200);
    EXPECT_EQ(view.statusQuadlet, 0xA5A5A5A5u);
}

TEST(TascamBlockCodecTests, RejectsUndersizedCaptureBlock) {
    const std::vector<uint32_t> tooSmall(19, 0u);  // needs 20 for 18 channels
    const auto view = DecodeCaptureBlock(tooSmall, 18u);
    EXPECT_EQ(view.pcm.size(), 0u);
}

//==============================================================================
// Surface state image (amdtp-tascam.c:129-176)
//==============================================================================

TEST(TascamSurfaceImageTests, AccumulatesByCounterModulo64) {
    SurfaceImage image;
    image.Apply(3u, 0x11u);
    image.Apply(3u + kStateImageEntries, 0x22u);  // wraps to index 3
    EXPECT_EQ(image.Value(3u), 0x22u);
}

TEST(TascamSurfaceImageTests, NotifiesChangeInWatchedRange) {
    SurfaceImage image;
    EXPECT_FALSE(image.Apply(7u, 0x00000000u).has_value());  // first write, no diff

    const auto change = image.Apply(7u, 0x00010000u);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->index, 7u);
    EXPECT_EQ(change->before, 0x00000000u);
    EXPECT_EQ(change->after, 0x00010000u);
}

TEST(TascamSurfaceImageTests, IgnoresChangesOutsideWatchedRange) {
    SurfaceImage image;
    // index > 4 && index < 16 is the notified window (amdtp-tascam.c:145).
    EXPECT_FALSE(image.Apply(4u, 0xFFFFFFFFu).has_value());
    EXPECT_FALSE(image.Apply(16u, 0xFFFFFFFFu).has_value());
    EXPECT_FALSE(image.Apply(63u, 0xFFFFFFFFu).has_value());
    // The image still updates.
    EXPECT_EQ(image.Value(4u), 0xFFFFFFFFu);
}

TEST(TascamSurfaceImageTests, MasksNoiseBitsOnIndices5And6) {
    SurfaceImage image;
    image.Apply(5u, 0x00000000u);
    // Low 16 bits are masked out for indices 5/6 (amdtp-tascam.c:148-151).
    EXPECT_FALSE(image.Apply(5u, 0x0000FFFFu).has_value());
    // High-bit changes notify.
    EXPECT_TRUE(image.Apply(5u, 0x00010000u).has_value());
}

TEST(TascamSurfaceImageTests, MasksNoiseBitsOnIndex8) {
    SurfaceImage image;
    image.Apply(8u, 0x00000000u);
    // 0x000f0f00 is masked out for index 8 (amdtp-tascam.c:152-153).
    EXPECT_FALSE(image.Apply(8u, 0x000F0F00u).has_value());
    EXPECT_TRUE(image.Apply(8u, 0x00100000u).has_value());
}

TEST(TascamSurfaceImageTests, ResetClearsImage) {
    SurfaceImage image;
    image.Apply(9u, 0x12345678u);
    image.Reset();
    EXPECT_EQ(image.Value(9u), 0u);
}
