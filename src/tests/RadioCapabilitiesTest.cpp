// QtTest invokes private slots through the generated meta-object.
#include "RadioCapabilities.h"

#include <QtTest>

class RadioCapabilitiesTest : public QObject
{
    Q_OBJECT

  private slots:
    void mapsBandBoundaries();
    void reportsCombinedBandEdges();
    void exposesStableBandMetadata();
    void populatesRequiredIc9700Capabilities();
    void commandMappingsRoundTrip();
};

void RadioCapabilitiesTest::mapsBandBoundaries()
{
    QCOMPARE(sdr9700::radioBandForFrequency(144000000), band2m);
    QCOMPARE(sdr9700::radioBandForFrequency(148000000), band2m);
    QCOMPARE(sdr9700::radioBandForFrequency(430000000), band70cm);
    QCOMPARE(sdr9700::radioBandForFrequency(450000000), band70cm);
    QCOMPARE(sdr9700::radioBandForFrequency(1240000000), band23cm);
    QCOMPARE(sdr9700::radioBandForFrequency(1300000000), band23cm);
    QCOMPARE(sdr9700::radioBandForFrequency(143999999), bandUnknown);
    QCOMPARE(sdr9700::radioBandForFrequency(1300000001), bandUnknown);
}

void RadioCapabilitiesTest::reportsCombinedBandEdges()
{
    quint64 start = 0;
    quint64 end = 0;

    QVERIFY(sdr9700::radioBandEdges(band2m, &start, &end));
    QCOMPARE(start, quint64(144000000));
    QCOMPARE(end, quint64(148000000));

    QVERIFY(sdr9700::radioBandEdges(band70cm, &start, &end));
    QCOMPARE(start, quint64(430000000));
    QCOMPARE(end, quint64(450000000));

    start = 1;
    end = 2;
    QVERIFY(!sdr9700::radioBandEdges(bandUnknown, &start, &end));
    QCOMPARE(start, quint64(1));
    QCOMPARE(end, quint64(2));
}

void RadioCapabilitiesTest::exposesStableBandMetadata()
{
    QCOMPARE(sdr9700::radioBandUiIndex(band2m), 0);
    QCOMPARE(sdr9700::radioBandUiIndex(band70cm), 1);
    QCOMPARE(sdr9700::radioBandUiIndex(band23cm), 2);
    QCOMPARE(sdr9700::radioBandUiIndex(bandUnknown), -1);
    QCOMPARE(sdr9700::radioBandShortLabel(band2m), QStringLiteral("2M"));
    QCOMPARE(sdr9700::radioBandShortLabel(bandUnknown), QStringLiteral("BAND"));
    QCOMPARE(sdr9700::radioBandMenuLabel(band2m), QStringLiteral("2M (144 MHZ)"));
    QCOMPARE(sdr9700::radioBandMenuLabel(bandUnknown), QStringLiteral("BAND"));
    QCOMPARE(sdr9700::radioBandMaxPowerWatts(band2m), 100.0);
    QCOMPARE(sdr9700::radioBandMaxPowerWatts(band70cm), 75.0);
    QCOMPARE(sdr9700::radioBandMaxPowerWatts(band23cm), 10.0);
    QCOMPARE(sdr9700::radioBandDefaultFrequency(band23cm), quint64(1296100000));
    QCOMPARE(sdr9700::radioBandMemoryKey(band70cm), 430);
}

void RadioCapabilitiesTest::populatesRequiredIc9700Capabilities()
{
    radioCapabilities capabilities;
    sdr9700::populateRadioCapabilities(capabilities);

    QCOMPARE(capabilities.modelName, QStringLiteral("IC-9700"));
    QCOMPARE(capabilities.manufacturer, manufIcom);
    QCOMPARE(capabilities.numReceiver, quint8(2));
    QCOMPARE(capabilities.numVFO, quint8(2));
    QVERIFY(capabilities.hasLan);
    QVERIFY(capabilities.hasEthernet);
    QVERIFY(capabilities.hasSpectrum);
    QVERIFY(capabilities.hasTransmit);
    QVERIFY(!capabilities.hasWiFi);
    QCOMPARE(capabilities.memGroups, quint16(3));
    QCOMPARE(capabilities.memories, quint16(107));
    QCOMPARE(capabilities.satMemories, quint16(99));
    QVERIFY(!capabilities.commands.isEmpty());
    QVERIFY(!capabilities.periodic.empty());
    QVERIFY(!capabilities.bands.empty());

    const auto scopeMode = std::find_if(capabilities.periodic.cbegin(), capabilities.periodic.cend(),
                                        [](const PeriodicType& periodic) { return periodic.func == funcScopeMode; });
    QVERIFY(scopeMode != capabilities.periodic.cend());
    QCOMPARE(scopeMode->receiver, qint8(-1));
}

void RadioCapabilitiesTest::commandMappingsRoundTrip()
{
    radioCapabilities capabilities;
    sdr9700::populateRadioCapabilities(capabilities);

    const FuncType frequencyGet = capabilities.commands.value(funcFreqGet);
    QCOMPARE(frequencyGet.data, QByteArray::fromHex("03"));
    QVERIFY(frequencyGet.getCmd);
    QVERIFY(!frequencyGet.setCmd);
    QCOMPARE(capabilities.commandsReverse.value(frequencyGet.data), funcFreqGet);

    const FuncType frequencySet = capabilities.commands.value(funcFreqSet);
    QCOMPARE(frequencySet.data, QByteArray::fromHex("05"));
    QVERIFY(!frequencySet.getCmd);
    QVERIFY(frequencySet.setCmd);
    QCOMPARE(capabilities.commandsReverse.value(frequencySet.data), funcFreqSet);

    const FuncType memoryToVfo = capabilities.commands.value(funcMemoryToVFO);
    QCOMPARE(memoryToVfo.data, QByteArray::fromHex("0a"));
    QVERIFY(memoryToVfo.getCmd);
    QVERIFY(!memoryToVfo.setCmd);
    QCOMPARE(capabilities.commandsReverse.value(memoryToVfo.data), funcMemoryToVFO);

    for (auto it = capabilities.commands.cbegin(); it != capabilities.commands.cend(); ++it)
    {
        QCOMPARE(capabilities.commandsReverse.value(it.value().data, funcNone), it.key());
    }
}

QTEST_APPLESS_MAIN(RadioCapabilitiesTest)
#include "RadioCapabilitiesTest.moc"
