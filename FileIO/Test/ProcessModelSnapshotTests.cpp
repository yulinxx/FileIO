#include <gtest/gtest.h>

#include "FileIO/ProcessModelSnapshot.h"
#include "Engine/Process/ProcessModel.h"

#include <string>

namespace
{
    Eg::ProcessModel makeSampleModel()
    {
        Eg::ProcessModel model;
        model.version = Eg::kProcessModelVersion;

        model.defaults.powerPercent = 62.0;
        model.defaults.speedMmPerSec = 180.0;
        model.defaults.rapidSpeedMmPerSec = 0.0;
        model.defaults.frequencyHz = 25000.0;
        model.defaults.kerfMm = 0.05;
        model.defaults.contourOrdering = 1;

        model.global.rapidSpeedMmPerSec = 320.0;
        model.global.chordToleranceMm = 0.02;
        model.global.emitArcs = false;
        model.global.skipHiddenLayers = false;
        model.global.emitTrailingLaserOff = false;

        Eg::LayerProcessEntry layer;
        layer.layerId = 7;
        layer.outputEnabled = false;
        layer.params.powerPercent = 75.0;
        layer.params.speedMmPerSec = 90.0;
        layer.params.passes = 2;
        layer.params.contourOrdering = 1;

        layer.detail.hasKerf = true;
        layer.detail.kerf.enabled = true;
        layer.detail.kerf.kerfMm = -0.1;
        layer.detail.kerf.arcToleranceMm = 0.005;

        layer.detail.hasLeadInOut = true;
        layer.detail.leadInOut.enabled = true;
        layer.detail.leadInOut.leadInLengthMm = 2.5;
        layer.detail.leadInOut.leadOutLengthMm = 1.5;
        layer.detail.leadInOut.rampAngleDeg = 12.0;
        layer.detail.leadInOut.leadInMode = Eg::LeadInMode::Arc;
        layer.detail.leadInOut.leadInAngleDeg = 45.0;
        layer.detail.leadInOut.leadInArcRadiusMm = 4.0;
        layer.detail.leadInOut.leadInArcAngleDeg = 120.0;
        layer.detail.leadInOut.leadOutMode = Eg::LeadInMode::Normal;
        layer.detail.leadInOut.leadOutAngleDeg = 30.0;
        layer.detail.leadInOut.checkScrapSide = false;

        layer.detail.hasPierce = true;
        layer.detail.pierce.enabled = true;
        layer.detail.pierce.pierceDelayUs = 800;
        layer.detail.pierce.piercePowerPercent = 40.0;
        layer.detail.pierce.pierceSpeedMmPerSec = 60.0;
        layer.detail.pierce.pierceCount = 2;
        layer.detail.pierce.dwellUs = 300;
        layer.detail.pierce.pierceOffsetMm = 0.4;
        layer.detail.pierce.prePierced = true;

        layer.detail.hasCorner = true;
        layer.detail.corner.strategy = 2;
        layer.detail.corner.radiusMm = 1.2;
        layer.detail.corner.overcutMm = 0.3;
        layer.detail.corner.slowdownFactor = 0.35;

        layer.detail.hasPowerCurve = true;
        layer.detail.powerCurve.enabled = true;
        Eg::PowerCurvePointSpec p0;
        p0.speedMmPerSec = 50.0;
        p0.powerPercent = 30.0;
        Eg::PowerCurvePointSpec p1;
        p1.speedMmPerSec = 200.0;
        p1.powerPercent = 90.0;
        layer.detail.powerCurve.points.push_back(p0);
        layer.detail.powerCurve.points.push_back(p1);
        layer.detail.powerCurve.startTransitionMm = 0.8;
        layer.detail.powerCurve.endTransitionMm = 1.1;
        layer.detail.powerCurve.transitionMinPowerPercent = 20.0;

        layer.detail.hasPassStrategy = true;
        layer.detail.passStrategy.adaptiveThickness = true;
        layer.detail.passStrategy.totalDepthLayers = 3;
        layer.detail.passStrategy.totalDepthMm = 4.5;

        layer.detail.hasCommonEdge = true;
        layer.detail.commonEdge.enabled = true;
        layer.detail.commonEdge.toleranceMm = 0.03;

        model.layers.push_back(layer);
        return model;
    }
}  // namespace

TEST(ProcessModelSnapshotTest, RoundTripPreservesAllFields)
{
    const Eg::ProcessModel original = makeSampleModel();

    const Fio::ProcessModelSnapshotResult snapshot = Fio::SerializeProcessModel(original);
    ASSERT_TRUE(snapshot.ok) << snapshot.error;
    EXPECT_EQ(Eg::kProcessModelVersion, snapshot.version);
    EXPECT_FALSE(snapshot.bytes.empty());
    EXPECT_NE(0u, snapshot.contentHash);

    Eg::ProcessModel restored;
    std::string error;
    ASSERT_TRUE(Fio::DeserializeProcessModel(snapshot.bytes, restored, error)) << error;

    EXPECT_EQ(original.version, restored.version);
    EXPECT_DOUBLE_EQ(original.defaults.powerPercent, restored.defaults.powerPercent);
    EXPECT_DOUBLE_EQ(original.defaults.speedMmPerSec, restored.defaults.speedMmPerSec);
    EXPECT_DOUBLE_EQ(original.defaults.frequencyHz, restored.defaults.frequencyHz);
    EXPECT_DOUBLE_EQ(original.defaults.kerfMm, restored.defaults.kerfMm);
    EXPECT_EQ(original.defaults.contourOrdering, restored.defaults.contourOrdering);

    EXPECT_DOUBLE_EQ(original.global.rapidSpeedMmPerSec, restored.global.rapidSpeedMmPerSec);
    EXPECT_DOUBLE_EQ(original.global.chordToleranceMm, restored.global.chordToleranceMm);
    EXPECT_EQ(original.global.emitArcs, restored.global.emitArcs);
    EXPECT_EQ(original.global.skipHiddenLayers, restored.global.skipHiddenLayers);
    EXPECT_EQ(original.global.emitTrailingLaserOff, restored.global.emitTrailingLaserOff);

    ASSERT_EQ(1u, restored.layers.size());
    const Eg::LayerProcessEntry& a = original.layers.front();
    const Eg::LayerProcessEntry& b = restored.layers.front();
    EXPECT_EQ(a.layerId, b.layerId);
    EXPECT_EQ(a.outputEnabled, b.outputEnabled);
    EXPECT_EQ(a.params.contourOrdering, b.params.contourOrdering);

    EXPECT_EQ(a.detail.hasKerf, b.detail.hasKerf);
    EXPECT_EQ(a.detail.hasLeadInOut, b.detail.hasLeadInOut);
    EXPECT_EQ(a.detail.hasPierce, b.detail.hasPierce);
    EXPECT_EQ(a.detail.hasCorner, b.detail.hasCorner);
    EXPECT_EQ(a.detail.hasPowerCurve, b.detail.hasPowerCurve);
    EXPECT_EQ(a.detail.hasPassStrategy, b.detail.hasPassStrategy);
    EXPECT_EQ(a.detail.hasCommonEdge, b.detail.hasCommonEdge);

    EXPECT_EQ(a.detail.kerf.enabled, b.detail.kerf.enabled);
    EXPECT_DOUBLE_EQ(a.detail.kerf.kerfMm, b.detail.kerf.kerfMm);
    EXPECT_DOUBLE_EQ(a.detail.kerf.arcToleranceMm, b.detail.kerf.arcToleranceMm);

    EXPECT_EQ(a.detail.leadInOut.enabled, b.detail.leadInOut.enabled);
    EXPECT_DOUBLE_EQ(a.detail.leadInOut.leadInLengthMm, b.detail.leadInOut.leadInLengthMm);
    EXPECT_DOUBLE_EQ(a.detail.leadInOut.leadOutLengthMm, b.detail.leadInOut.leadOutLengthMm);
    EXPECT_DOUBLE_EQ(a.detail.leadInOut.rampAngleDeg, b.detail.leadInOut.rampAngleDeg);
    EXPECT_EQ(a.detail.leadInOut.leadInMode, b.detail.leadInOut.leadInMode);
    EXPECT_DOUBLE_EQ(a.detail.leadInOut.leadInArcRadiusMm, b.detail.leadInOut.leadInArcRadiusMm);
    EXPECT_DOUBLE_EQ(a.detail.leadInOut.leadInArcAngleDeg, b.detail.leadInOut.leadInArcAngleDeg);
    EXPECT_EQ(a.detail.leadInOut.leadOutMode, b.detail.leadInOut.leadOutMode);
    EXPECT_EQ(a.detail.leadInOut.checkScrapSide, b.detail.leadInOut.checkScrapSide);

    EXPECT_EQ(a.detail.pierce.enabled, b.detail.pierce.enabled);
    EXPECT_EQ(a.detail.pierce.pierceDelayUs, b.detail.pierce.pierceDelayUs);
    EXPECT_DOUBLE_EQ(a.detail.pierce.piercePowerPercent, b.detail.pierce.piercePowerPercent);
    EXPECT_DOUBLE_EQ(a.detail.pierce.pierceSpeedMmPerSec, b.detail.pierce.pierceSpeedMmPerSec);
    EXPECT_EQ(a.detail.pierce.pierceCount, b.detail.pierce.pierceCount);
    EXPECT_EQ(a.detail.pierce.dwellUs, b.detail.pierce.dwellUs);
    EXPECT_DOUBLE_EQ(a.detail.pierce.pierceOffsetMm, b.detail.pierce.pierceOffsetMm);
    EXPECT_EQ(a.detail.pierce.prePierced, b.detail.pierce.prePierced);

    EXPECT_EQ(a.detail.corner.strategy, b.detail.corner.strategy);
    EXPECT_DOUBLE_EQ(a.detail.corner.radiusMm, b.detail.corner.radiusMm);
    EXPECT_DOUBLE_EQ(a.detail.corner.overcutMm, b.detail.corner.overcutMm);
    EXPECT_DOUBLE_EQ(a.detail.corner.slowdownFactor, b.detail.corner.slowdownFactor);

    EXPECT_EQ(a.detail.powerCurve.enabled, b.detail.powerCurve.enabled);
    ASSERT_EQ(a.detail.powerCurve.points.size(), b.detail.powerCurve.points.size());
    for (size_t i = 0; i < a.detail.powerCurve.points.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(a.detail.powerCurve.points[i].speedMmPerSec,
            b.detail.powerCurve.points[i].speedMmPerSec);
        EXPECT_DOUBLE_EQ(a.detail.powerCurve.points[i].powerPercent,
            b.detail.powerCurve.points[i].powerPercent);
    }
    EXPECT_DOUBLE_EQ(a.detail.powerCurve.startTransitionMm, b.detail.powerCurve.startTransitionMm);
    EXPECT_DOUBLE_EQ(a.detail.powerCurve.endTransitionMm, b.detail.powerCurve.endTransitionMm);
    EXPECT_DOUBLE_EQ(a.detail.powerCurve.transitionMinPowerPercent,
        b.detail.powerCurve.transitionMinPowerPercent);

    EXPECT_EQ(a.detail.passStrategy.adaptiveThickness, b.detail.passStrategy.adaptiveThickness);
    EXPECT_EQ(a.detail.passStrategy.totalDepthLayers, b.detail.passStrategy.totalDepthLayers);
    EXPECT_DOUBLE_EQ(a.detail.passStrategy.totalDepthMm, b.detail.passStrategy.totalDepthMm);

    EXPECT_EQ(a.detail.commonEdge.enabled, b.detail.commonEdge.enabled);
    EXPECT_DOUBLE_EQ(a.detail.commonEdge.toleranceMm, b.detail.commonEdge.toleranceMm);
}

TEST(ProcessModelSnapshotTest, RejectsCorruptedBytes)
{
    Eg::ProcessModel restored;
    std::string error;
    EXPECT_FALSE(Fio::DeserializeProcessModel(std::string("\xff\xff", 2), restored, error));
    EXPECT_FALSE(error.empty());
}

TEST(ProcessModelSnapshotTest, NewerVersionIsCarriedForValidation)
{
    Eg::ProcessModel original = makeSampleModel();
    original.version = Eg::kProcessModelVersion + 1;

    const Fio::ProcessModelSnapshotResult snapshot = Fio::SerializeProcessModel(original);
    ASSERT_TRUE(snapshot.ok) << snapshot.error;

    Eg::ProcessModel restored;
    std::string error;
    ASSERT_TRUE(Fio::DeserializeProcessModel(snapshot.bytes, restored, error)) << error;
    EXPECT_EQ(Eg::kProcessModelVersion + 1, restored.version);

    // 序列化层不降级；由模型校验统一拒绝。
    std::vector<Eg::ProcessModelIssue> issues;
    EXPECT_FALSE(Eg::validateProcessModel(restored, issues));
}
