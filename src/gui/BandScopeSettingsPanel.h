// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QColor>
#include <QWidget>

class QPushButton;

class BandScopeSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit BandScopeSettingsPanel(QWidget* parent = nullptr);

  signals:
    void centerLineColorChanged(const QColor& color);

  private slots:
    void chooseCenterLineColor();
    void resetCenterLineColor();

  private:
    void setCenterLineColor(const QColor& color, bool persist);
    void updateColorButton();

    QPushButton* m_colorButton{nullptr};
    QPushButton* m_resetButton{nullptr};
    QColor m_centerLineColor;
};
