#include "BandScopeSettingsPanel.h"

#include "AppSettings.h"
#include "SettingsPanelStyle.h"
#include "UiTheme.h"

#include <QColorDialog>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace
{
constexpr auto kBandscopeCenterLineColorSettingsKey = "BandscopeCenterLineColor";
constexpr auto kBandscopeBackgroundColorSettingsKey = "BandscopeBackgroundColor";
constexpr auto kBandscopeGridLineColorSettingsKey = "BandscopeGridLineColor";
constexpr auto kBandscopeGridDensitySettingsKey = "BandscopeGridDensity";
const QColor kDefaultCenterLineColor(0xf5, 0xf7, 0xf8);
const QColor kDefaultBackgroundColor(0x0b, 0x3f, 0x55);
const QColor kDefaultGridLineColor(0xc8, 0xf1, 0xf5);
constexpr int kDefaultGridDensity = 1;
constexpr int kMinGridDensity = 0;
constexpr int kMaxGridDensity = 2;

QColor storedColor(const char* key, const QColor& defaultColor)
{
    const QColor color(
        AppSettings::instance().value(QString::fromLatin1(key), defaultColor.name(QColor::HexRgb)).toString());
    return color.isValid() ? color : defaultColor;
}

int storedGridDensity()
{
    return qBound(kMinGridDensity,
                  AppSettings::instance()
                      .value(QString::fromLatin1(kBandscopeGridDensitySettingsKey), kDefaultGridDensity)
                      .toInt(),
                  kMaxGridDensity);
}
} // namespace

BandScopeSettingsPanel::BandScopeSettingsPanel(QWidget* parent)
    : QWidget(parent),
      m_centerLineColor(storedColor(kBandscopeCenterLineColorSettingsKey, kDefaultCenterLineColor)),
      m_backgroundColor(storedColor(kBandscopeBackgroundColorSettingsKey, kDefaultBackgroundColor)),
      m_gridLineColor(storedColor(kBandscopeGridLineColorSettingsKey, kDefaultGridLineColor)),
      m_gridDensity(storedGridDensity())
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 4, 12, 0);
    root->setSpacing(8);

    auto* centerLineGroup = new QGroupBox("Center Line", this);
    centerLineGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* centerLineLayout = new QVBoxLayout(centerLineGroup);
    centerLineLayout->setContentsMargins(10, 12, 10, 10);
    centerLineLayout->setSpacing(8);
    centerLineLayout->addLayout(
        makeColorRow(centerLineGroup, QStringLiteral("Color:"), &m_centerLineColorButton, &m_centerLineResetButton));
    root->addWidget(centerLineGroup);

    auto* backgroundGroup = new QGroupBox("Background", this);
    backgroundGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* backgroundLayout = new QVBoxLayout(backgroundGroup);
    backgroundLayout->setContentsMargins(10, 12, 10, 10);
    backgroundLayout->setSpacing(8);
    backgroundLayout->addLayout(
        makeColorRow(backgroundGroup, QStringLiteral("Color:"), &m_backgroundColorButton, &m_backgroundResetButton));
    root->addWidget(backgroundGroup);

    auto* gridGroup = new QGroupBox("Gridlines", this);
    gridGroup->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* gridLayout = new QVBoxLayout(gridGroup);
    gridLayout->setContentsMargins(10, 12, 10, 10);
    gridLayout->setSpacing(8);

    auto* gridRow = new QHBoxLayout;
    auto* gridColorLabel = new QLabel("Color:", gridGroup);
    m_gridLineColorButton = new QPushButton(gridGroup);
    m_gridLineColorButton->setMinimumWidth(110);
    auto* densityLabel = new QLabel("Density:", gridGroup);
    m_gridDensityCombo = new QComboBox(gridGroup);
    m_gridDensityCombo->addItem(QStringLiteral("Fewer"), 0);
    m_gridDensityCombo->addItem(QStringLiteral("Normal"), 1);
    m_gridDensityCombo->addItem(QStringLiteral("More"), 2);
    m_gridLineResetButton = new QPushButton("Reset", gridGroup);
    if (const int index = m_gridDensityCombo->findData(m_gridDensity); index >= 0)
    {
        m_gridDensityCombo->setCurrentIndex(index);
    }
    gridRow->addWidget(gridColorLabel);
    gridRow->addWidget(m_gridLineColorButton);
    gridRow->addSpacing(16);
    gridRow->addWidget(densityLabel);
    gridRow->addWidget(m_gridDensityCombo);
    gridRow->addStretch(1);
    gridRow->addWidget(m_gridLineResetButton);
    gridLayout->addLayout(gridRow);
    root->addWidget(gridGroup);

    connect(m_centerLineColorButton, &QPushButton::clicked, this, &BandScopeSettingsPanel::chooseCenterLineColor);
    connect(m_centerLineResetButton, &QPushButton::clicked, this, &BandScopeSettingsPanel::resetCenterLineColor);
    connect(m_backgroundColorButton, &QPushButton::clicked, this, &BandScopeSettingsPanel::chooseBackgroundColor);
    connect(m_backgroundResetButton, &QPushButton::clicked, this, &BandScopeSettingsPanel::resetBackgroundColor);
    connect(m_gridLineColorButton, &QPushButton::clicked, this, &BandScopeSettingsPanel::chooseGridLineColor);
    connect(m_gridLineResetButton, &QPushButton::clicked, this, &BandScopeSettingsPanel::resetGridLineColor);
    connect(m_gridDensityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (m_gridDensityCombo)
                {
                    setGridDensity(m_gridDensityCombo->currentData().toInt(), true);
                }
            });

    updateColorButton(m_centerLineColorButton, m_centerLineColor);
    updateColorButton(m_backgroundColorButton, m_backgroundColor);
    updateColorButton(m_gridLineColorButton, m_gridLineColor);
    root->addStretch(1);
}

QHBoxLayout* BandScopeSettingsPanel::makeColorRow(QWidget* parent, const QString& labelText, QPushButton** colorButton,
                                                  QPushButton** resetButton)
{
    auto* row = new QHBoxLayout;
    auto* label = new QLabel(labelText, parent);
    *colorButton = new QPushButton(parent);
    (*colorButton)->setMinimumWidth(110);
    *resetButton = new QPushButton("Reset", parent);
    row->addWidget(label);
    row->addWidget(*colorButton);
    row->addStretch(1);
    row->addWidget(*resetButton);
    return row;
}

void BandScopeSettingsPanel::chooseCenterLineColor()
{
    const QColor color = QColorDialog::getColor(m_centerLineColor, this, QStringLiteral("Band Scope Center Line"));
    if (color.isValid())
    {
        setCenterLineColor(color, true);
    }
}

void BandScopeSettingsPanel::chooseBackgroundColor()
{
    const QColor color = QColorDialog::getColor(m_backgroundColor, this, QStringLiteral("Band Scope Background"));
    if (color.isValid())
    {
        setBackgroundColor(color, true);
    }
}

void BandScopeSettingsPanel::chooseGridLineColor()
{
    const QColor color = QColorDialog::getColor(m_gridLineColor, this, QStringLiteral("Band Scope Gridlines"));
    if (color.isValid())
    {
        setGridLineColor(color, true);
    }
}

void BandScopeSettingsPanel::resetCenterLineColor()
{
    setCenterLineColor(kDefaultCenterLineColor, true);
}

void BandScopeSettingsPanel::resetBackgroundColor()
{
    setBackgroundColor(kDefaultBackgroundColor, true);
}

void BandScopeSettingsPanel::resetGridLineColor()
{
    setGridLineColor(kDefaultGridLineColor, true);
}

void BandScopeSettingsPanel::setCenterLineColor(const QColor& color, bool persist)
{
    if (!color.isValid())
    {
        return;
    }

    const QColor normalized(color.red(), color.green(), color.blue());
    if (m_centerLineColor == normalized)
    {
        return;
    }

    m_centerLineColor = normalized;
    if (persist)
    {
        AppSettings::instance().setValue(QString::fromLatin1(kBandscopeCenterLineColorSettingsKey),
                                         m_centerLineColor.name(QColor::HexRgb));
    }
    updateColorButton(m_centerLineColorButton, m_centerLineColor);
    Q_EMIT centerLineColorChanged(m_centerLineColor);
}

void BandScopeSettingsPanel::setBackgroundColor(const QColor& color, bool persist)
{
    if (!color.isValid())
    {
        return;
    }

    const QColor normalized(color.red(), color.green(), color.blue());
    if (m_backgroundColor == normalized)
    {
        return;
    }

    m_backgroundColor = normalized;
    if (persist)
    {
        AppSettings::instance().setValue(QString::fromLatin1(kBandscopeBackgroundColorSettingsKey),
                                         m_backgroundColor.name(QColor::HexRgb));
    }
    updateColorButton(m_backgroundColorButton, m_backgroundColor);
    Q_EMIT backgroundColorChanged(m_backgroundColor);
}

void BandScopeSettingsPanel::setGridLineColor(const QColor& color, bool persist)
{
    if (!color.isValid())
    {
        return;
    }

    const QColor normalized(color.red(), color.green(), color.blue());
    if (m_gridLineColor == normalized)
    {
        return;
    }

    m_gridLineColor = normalized;
    if (persist)
    {
        AppSettings::instance().setValue(QString::fromLatin1(kBandscopeGridLineColorSettingsKey),
                                         m_gridLineColor.name(QColor::HexRgb));
    }
    updateColorButton(m_gridLineColorButton, m_gridLineColor);
    Q_EMIT gridLineColorChanged(m_gridLineColor);
}

void BandScopeSettingsPanel::setGridDensity(int density, bool persist)
{
    const int normalized = qBound(kMinGridDensity, density, kMaxGridDensity);
    if (m_gridDensity == normalized)
    {
        return;
    }

    m_gridDensity = normalized;
    if (persist)
    {
        AppSettings::instance().setValue(QString::fromLatin1(kBandscopeGridDensitySettingsKey), m_gridDensity);
    }
    if (m_gridDensityCombo)
    {
        const QSignalBlocker blocker(m_gridDensityCombo);
        if (const int index = m_gridDensityCombo->findData(m_gridDensity); index >= 0)
        {
            m_gridDensityCombo->setCurrentIndex(index);
        }
    }
    Q_EMIT gridDensityChanged(m_gridDensity);
}

void BandScopeSettingsPanel::updateColorButton(QPushButton* button, const QColor& color)
{
    if (!button)
    {
        return;
    }

    const QString colorName = color.name(QColor::HexRgb);
    button->setText(colorName.toUpper());
    button->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; color: %3; "
                       "padding: 4px 12px; font-weight: bold; }"
                       "QPushButton:hover { border-color: %4; }")
            .arg(colorName, QLatin1String(UiTheme::Color::BorderLight),
                 color.lightness() > 140 ? QLatin1String(UiTheme::Color::PanelDark)
                                         : QLatin1String(UiTheme::Color::TextBright),
                 QLatin1String(UiTheme::Color::AccentBright)));
}
