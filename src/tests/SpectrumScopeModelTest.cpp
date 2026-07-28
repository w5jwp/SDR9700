// QtTest invokes private slots through the generated meta-object.
#include "SpectrumScopeModel.h"

#include <QtTest>

#include <limits>

class SpectrumScopeModelTest : public QObject
{
    Q_OBJECT

  private slots:
    void startsWithDefaultRange();
    void ingestsAndNormalizesSpectrumRanges();
    void rejectsInvalidSpectrumRanges();
    void constrainsDisplayToFrequencyLimits();
    void displayCenterHoldRejectsStaleFrames();
};

void SpectrumScopeModelTest::startsWithDefaultRange()
{
    const SpectrumScopeModel model;

    QCOMPARE(model.startMhz(), 144.5);
    QCOMPARE(model.endMhz(), 145.5);
    QCOMPARE(model.bandwidthMhz(), 1.0);
}

void SpectrumScopeModelTest::ingestsAndNormalizesSpectrumRanges()
{
    SpectrumScopeModel model;
    QVector<float> emittedLevels;
    double emittedStart = 0.0;
    double emittedEnd = 0.0;
    connect(&model, &SpectrumScopeModel::spectrumReady, this,
            [&emittedLevels, &emittedStart, &emittedEnd](const QVector<float>& levels, double start, double end, bool)
            {
                emittedLevels = levels;
                emittedStart = start;
                emittedEnd = end;
            });

    model.ingestSpectrum({1.0F, 2.0F, 3.0F}, 147.0, 146.0, false);

    QCOMPARE(emittedLevels, QVector<float>({3.0F, 2.0F, 1.0F}));
    QCOMPARE(emittedStart, 146.0);
    QCOMPARE(emittedEnd, 147.0);
    QCOMPARE(model.startMhz(), 146.0);
    QCOMPARE(model.endMhz(), 147.0);
}

void SpectrumScopeModelTest::rejectsInvalidSpectrumRanges()
{
    SpectrumScopeModel model;
    QSignalSpy rangeSpy(&model, &SpectrumScopeModel::rangeChanged);
    QSignalSpy spectrumSpy(&model, &SpectrumScopeModel::spectrumReady);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    model.ingestSpectrum({1.0F}, nan, 146.0, false);
    model.ingestSpectrum({1.0F}, 146.0, 146.0005, false);

    QCOMPARE(rangeSpy.count(), 0);
    QCOMPARE(spectrumSpy.count(), 0);
}

void SpectrumScopeModelTest::constrainsDisplayToFrequencyLimits()
{
    SpectrumScopeModel model;
    model.ingestSpectrum({1.0F, 2.0F}, 144.0, 148.0, false);
    model.setFrequencyLimits(144.0, 146.0);

    QCOMPARE(model.startMhz(), 144.0);
    QCOMPARE(model.endMhz(), 146.0);
    QCOMPARE(model.bandwidthMhz(), 2.0);

    model.centerOnFrequency(148.0);
    QCOMPARE(model.startMhz(), 144.0);
    QCOMPARE(model.endMhz(), 146.0);
}

void SpectrumScopeModelTest::displayCenterHoldRejectsStaleFrames()
{
    SpectrumScopeModel model;
    QSignalSpy spectrumSpy(&model, &SpectrumScopeModel::spectrumReady);

    model.holdDisplayCenter(146.5, 146.6);
    model.ingestSpectrum({1.0F, 2.0F, 3.0F}, 146.0, 147.0, false);
    QCOMPARE(spectrumSpy.count(), 0);

    model.ingestSpectrum({1.0F, 2.0F, 3.0F}, 146.1, 147.1, false);
    QCOMPARE(spectrumSpy.count(), 1);
    QCOMPARE(model.startMhz(), 146.1);
    QCOMPARE(model.endMhz(), 147.1);
}

QTEST_GUILESS_MAIN(SpectrumScopeModelTest)
#include "SpectrumScopeModelTest.moc"
