// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class BandscopeCanvas;
class QComboBox;
class WaterfallCanvas;

class BandscopeDisplay : public QWidget
{
    Q_OBJECT

  public:
    struct SpanChoice
    {
        quint64 hz;
        QString label;
    };

    explicit BandscopeDisplay(QWidget* parent = nullptr);

    void setSpanChoices(const QVector<SpanChoice>& choices);
    void setCurrentSpanHz(quint64 hz);
    void setFrequencyRange(double startMhz, double endMhz);
    void setDataFrequencyRange(double startMhz, double endMhz);
    void setVfoFrequency(double freqMhz);
    void setInteractionLocked(bool locked);
    void setInvertMouseWheel(bool invert);
    int spectrumPaneHeight() const;
    void setSpectrumPaneHeight(int height);
    void updateSpectrum(const QVector<float>& levels, bool outOfRange);
    void clearDisplay();
    void setFilterWidth(int lowHz, int highHz);
    int freqToX(double mhz) const;

    QSize sizeHint() const override { return {900, 340}; }

  signals:
    void frequencyClicked(double freqMhz);
    void tuneStepRequested(int steps);
    void tuneDragStarted();
    void tuneDragRequested(double deltaMhz);
    void spanSelected(quint64 hz);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    static int splitterHeight() { return 2; }
    void updateSpanComboGeometry();
    int defaultSpectrumHeight() const;
    int constrainedSpectrumHeight(int requested) const;
    int currentSpectrumHeight() const;
    void updateChildGeometry();

    BandscopeCanvas* m_bandscopeCanvas{nullptr};
    QWidget* m_splitter{nullptr};
    WaterfallCanvas* m_waterfallCanvas{nullptr};
    QComboBox* m_spanCombo{nullptr};
    int m_spectrumHeight{-1};
};
