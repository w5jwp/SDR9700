// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QColor>
#include <QWidget>

class QComboBox;
class QHBoxLayout;
class QPushButton;

class BandScopeSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit BandScopeSettingsPanel(QWidget* parent = nullptr);

  signals:
    void centerLineColorChanged(const QColor& color);
    void backgroundColorChanged(const QColor& color);
    void gridLineColorChanged(const QColor& color);
    void gridDensityChanged(int density);

  private slots:
    void chooseCenterLineColor();
    void chooseBackgroundColor();
    void chooseGridLineColor();
    void resetCenterLineColor();
    void resetBackgroundColor();
    void resetGridLineColor();

  private:
    QHBoxLayout* makeColorRow(QWidget* parent, const QString& labelText, QPushButton** colorButton,
                              QPushButton** resetButton);
    void setCenterLineColor(const QColor& color, bool persist);
    void setBackgroundColor(const QColor& color, bool persist);
    void setGridLineColor(const QColor& color, bool persist);
    void setGridDensity(int density, bool persist);
    void updateColorButton(QPushButton* button, const QColor& color);

    QPushButton* m_centerLineColorButton{nullptr};
    QPushButton* m_centerLineResetButton{nullptr};
    QPushButton* m_backgroundColorButton{nullptr};
    QPushButton* m_backgroundResetButton{nullptr};
    QPushButton* m_gridLineColorButton{nullptr};
    QPushButton* m_gridLineResetButton{nullptr};
    QComboBox* m_gridDensityCombo{nullptr};
    QColor m_centerLineColor;
    QColor m_backgroundColor;
    QColor m_gridLineColor;
    int m_gridDensity{1};
};
