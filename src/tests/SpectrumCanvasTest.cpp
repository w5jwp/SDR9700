#include "SpectrumScopeCanvas.h"
#include "SpectrumScopeDisplay.h"
#include "WaterfallCanvas.h"

#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

class SpectrumCanvasTest : public QObject
{
    Q_OBJECT

  private slots:
    void mapsFrequencyAcrossClosedPixelRange();
    void alignsWaterfallWithSpectrumFrequencyPlot();
    void normalizesReversedRange();
    void emitsFrequencyForClickWithoutDrag();
    void ignoresClicksOutsidePlotAndWhileLocked();
    void emitsWheelStepsAndHonorsInversion();
    void paintsEmptyAndPopulatedData();
};

void SpectrumCanvasTest::mapsFrequencyAcrossClosedPixelRange()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.setFrequencyRange(144.0, 148.0);
    QCOMPARE(canvas.freqToX(144.0), 0);
    QCOMPARE(canvas.freqToX(148.0), 429);
    QCOMPARE(canvas.freqToX(146.0), 429 / 2);
}

void SpectrumCanvasTest::alignsWaterfallWithSpectrumFrequencyPlot()
{
    SpectrumScopeDisplay display;
    display.resize(700, 500);
    display.show();

    auto* spectrum = display.findChild<SpectrumScopeCanvas*>();
    auto* waterfall = display.findChild<WaterfallCanvas*>();
    QVERIFY(spectrum != nullptr);
    QVERIFY(waterfall != nullptr);
    QCOMPARE(waterfall->geometry().left(), spectrum->freqToX(144.0));
    QCOMPARE(waterfall->geometry().right(), spectrum->freqToX(146.0));

    display.resize(900, 600);
    QCoreApplication::processEvents();
    QCOMPARE(waterfall->geometry().left(), spectrum->freqToX(144.0));
    QCOMPARE(waterfall->geometry().right(), spectrum->freqToX(146.0));
}

void SpectrumCanvasTest::normalizesReversedRange()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.setFrequencyRange(148.0, 144.0);
    QVERIFY(canvas.freqToX(144.0) < canvas.freqToX(148.0));
}

void SpectrumCanvasTest::emitsFrequencyForClickWithoutDrag()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.setFrequencyRange(144.0, 148.0);
    canvas.show();
    QSignalSpy clickSpy(&canvas, &SpectrumScopeCanvas::frequencyClicked);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(canvas.freqToX(146.0), 80));
    QCOMPARE(clickSpy.count(), 1);
    QVERIFY(qAbs(clickSpy.takeFirst().at(0).toDouble() - 146.0) < 0.02);
}

void SpectrumCanvasTest::ignoresClicksOutsidePlotAndWhileLocked()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();
    QSignalSpy clickSpy(&canvas, &SpectrumScopeCanvas::frequencyClicked);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(5, canvas.height() - 1));
    canvas.setInteractionLocked(true);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(230, 80));
    QCOMPARE(clickSpy.count(), 0);
}

void SpectrumCanvasTest::emitsWheelStepsAndHonorsInversion()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();
    QSignalSpy wheelSpy(&canvas, &SpectrumScopeCanvas::wheelStepRequested);
    const QPointF position(230, 80);
    QWheelEvent forward(position, canvas.mapToGlobal(position.toPoint()), QPoint(), QPoint(0, 120), Qt::NoButton,
                        Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&canvas, &forward);
    QCOMPARE(wheelSpy.takeFirst().at(0).toInt(), 1);

    canvas.setInvertMouseWheel(true);
    QWheelEvent inverted(position, canvas.mapToGlobal(position.toPoint()), QPoint(), QPoint(0, 120), Qt::NoButton,
                         Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&canvas, &inverted);
    QCOMPARE(wheelSpy.takeFirst().at(0).toInt(), -1);
}

void SpectrumCanvasTest::paintsEmptyAndPopulatedData()
{
    SpectrumScopeCanvas canvas;
    canvas.resize(430, 240);
    canvas.show();
    canvas.clearDisplay();
    QVERIFY(!canvas.grab().isNull());
    canvas.updateSpectrum({0.0f, 40.0f, 80.0f, 120.0f, 160.0f}, false);
    QVERIFY(!canvas.grab().isNull());
    canvas.updateSpectrum({}, true);
    QVERIFY(!canvas.grab().isNull());
}

QTEST_MAIN(SpectrumCanvasTest)

#include "SpectrumCanvasTest.moc"
