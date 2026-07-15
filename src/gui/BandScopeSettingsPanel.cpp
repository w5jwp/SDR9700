#include "BandScopeSettingsPanel.h"

#include "AppSettings.h"
#include "SettingsPanelStyle.h"
#include "UiTheme.h"

#include <QColorDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
constexpr auto kBandscopeCenterLineColorSettingsKey = "BandscopeCenterLineColor";
const QColor kDefaultCenterLineColor(0xf5, 0xf7, 0xf8);

QColor storedCenterLineColor()
{
    const QColor color(AppSettings::instance()
                           .value(QString::fromLatin1(kBandscopeCenterLineColorSettingsKey),
                                  kDefaultCenterLineColor.name(QColor::HexRgb))
                           .toString());
    return color.isValid() ? color : kDefaultCenterLineColor;
}
} // namespace

BandScopeSettingsPanel::BandScopeSettingsPanel(QWidget* parent)
    : QWidget(parent), m_centerLineColor(storedCenterLineColor())
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 4, 12, 0);
    root->setSpacing(8);

    auto* group = new QGroupBox("Center Line", this);
    group->setStyleSheet(sdr9700::ui::settingsGroupBoxStyle());
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(10, 12, 10, 10);
    layout->setSpacing(8);

    auto* row = new QHBoxLayout;
    auto* label = new QLabel("Color:", group);
    m_colorButton = new QPushButton(group);
    m_colorButton->setMinimumWidth(110);
    m_resetButton = new QPushButton("Reset", group);
    row->addWidget(label);
    row->addWidget(m_colorButton);
    row->addStretch(1);
    row->addWidget(m_resetButton);
    layout->addLayout(row);

    connect(m_colorButton, &QPushButton::clicked, this, &BandScopeSettingsPanel::chooseCenterLineColor);
    connect(m_resetButton, &QPushButton::clicked, this, &BandScopeSettingsPanel::resetCenterLineColor);

    updateColorButton();
    root->addWidget(group);
    root->addStretch(1);
}

void BandScopeSettingsPanel::chooseCenterLineColor()
{
    const QColor color = QColorDialog::getColor(m_centerLineColor, this, QStringLiteral("Band Scope Center Line"));
    if (color.isValid())
    {
        setCenterLineColor(color, true);
    }
}

void BandScopeSettingsPanel::resetCenterLineColor()
{
    setCenterLineColor(kDefaultCenterLineColor, true);
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
    updateColorButton();
    Q_EMIT centerLineColorChanged(m_centerLineColor);
}

void BandScopeSettingsPanel::updateColorButton()
{
    if (!m_colorButton)
    {
        return;
    }

    const QString colorName = m_centerLineColor.name(QColor::HexRgb);
    m_colorButton->setText(colorName.toUpper());
    m_colorButton->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 3px; color: %3; "
                       "padding: 4px 12px; font-weight: bold; }"
                       "QPushButton:hover { border-color: %4; }")
            .arg(colorName, QLatin1String(UiTheme::Color::BorderLight),
                 m_centerLineColor.lightness() > 140 ? QLatin1String(UiTheme::Color::PanelDark)
                                                     : QLatin1String(UiTheme::Color::TextBright),
                 QLatin1String(UiTheme::Color::AccentBright)));
}
