#include "DtmfDialog.h"
#include "UiTheme.h"

#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace
{
constexpr int kMaxDtmfDigits = 16;

struct DtmfKey
{
    const char* label;
    bool isAlpha;
};

constexpr DtmfKey kKeys[4][4] = {
    {{"1", false}, {"2", false}, {"3", false}, {"A", true}},
    {{"4", false}, {"5", false}, {"6", false}, {"B", true}},
    {{"7", false}, {"8", false}, {"9", false}, {"C", true}},
    {{"*", false}, {"0", false}, {"#", false}, {"D", true}},
};
} // namespace

DtmfDialog::DtmfDialog(QWidget* parent) : sdr9700::ui::UtilityWindow(QStringLiteral("DTMF"), parent)
{
    setFixedWidth(260);
    setStyleSheet(QStringLiteral("DtmfDialog { background: %1; border: 1px solid %2; }")
                      .arg(QLatin1String(UiTheme::Color::Panel), QLatin1String(UiTheme::Color::Border)));

    auto* root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    auto* titleBar = new sdr9700::ui::UtilityTitleBar(QStringLiteral("DTMF"), this);
    connect(titleBar->closeButton(), &QPushButton::clicked, this, &QWidget::close);
    root->addWidget(titleBar);

    // Content
    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setSpacing(8);
    contentLayout->setContentsMargins(12, 10, 12, 12);
    root->addWidget(content);

    m_display = new QLineEdit(content);
    m_display->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_display->setPlaceholderText(QStringLiteral("Digits…"));
    m_display->setMaxLength(kMaxDtmfDigits);
    m_display->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Da-d*#]*")), m_display));
    m_display->setStyleSheet(QStringLiteral("QLineEdit {"
                                            "  background: %1; border: 1px solid %2; border-radius: 3px;"
                                            "  color: %3; padding: 0 8px; font-size: 18px; font-weight: bold;"
                                            "  letter-spacing: 2px;"
                                            "}"
                                            "QLineEdit:focus { border-color: %4; }")
                                 .arg(QLatin1String(UiTheme::Color::Field), QLatin1String(UiTheme::Color::BorderFocus),
                                      QLatin1String(UiTheme::Color::TextField), QLatin1String(UiTheme::Color::Accent)));
    m_display->setFixedHeight(44);
    contentLayout->addWidget(m_display);

    connect(m_display, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_sendButton->setEnabled(!text.isEmpty()); });

    const QString keyStyle =
        QStringLiteral("QPushButton {"
                       "  background: %1; border: 1px solid %2; border-radius: 3px;"
                       "  color: %3; font-size: 15px; font-weight: bold;"
                       "}"
                       "QPushButton:hover { background: %4; border-color: %5; }"
                       "QPushButton:pressed { background: %6; }")
            .arg(QLatin1String(UiTheme::Color::Button), QLatin1String(UiTheme::Color::Border),
                 QLatin1String(UiTheme::Color::TextPrimary), QLatin1String(UiTheme::Color::ButtonHover),
                 QLatin1String(UiTheme::Color::ButtonHoverBorder), QLatin1String(UiTheme::Color::AccentDark));

    const QString alphaKeyStyle =
        QStringLiteral("QPushButton {"
                       "  background: %1; border: 1px solid %2; border-radius: 3px;"
                       "  color: %3; font-size: 13px; font-weight: bold;"
                       "}"
                       "QPushButton:hover { background: %4; border-color: %5; }"
                       "QPushButton:pressed { background: %6; }")
            .arg(QLatin1String(UiTheme::Color::AccentDark), QLatin1String(UiTheme::Color::Accent),
                 QLatin1String(UiTheme::Color::Accent), QLatin1String(UiTheme::Color::AccentHover),
                 QLatin1String(UiTheme::Color::AccentBright), QLatin1String(UiTheme::Color::Accent));

    auto* grid = new QGridLayout;
    grid->setSpacing(4);

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            const QString label = QString::fromLatin1(kKeys[row][col].label);
            const bool isAlpha = kKeys[row][col].isAlpha;
            auto* btn = new QPushButton(label, content);
            btn->setFixedSize(52, 38);
            btn->setStyleSheet(isAlpha ? alphaKeyStyle : keyStyle);
            connect(btn, &QPushButton::clicked, this, [this, label]() { appendDigit(label); });
            grid->addWidget(btn, row, col);
            m_keyButtons.append(btn);
        }
    }
    contentLayout->addLayout(grid);

    const QString ctrlStyle =
        QStringLiteral("QPushButton {"
                       "  background: %1; border: 1px solid %2; border-radius: 3px;"
                       "  color: %3; font-size: 13px; font-weight: bold;"
                       "}"
                       "QPushButton:hover { background: %4; border-color: %5; }"
                       "QPushButton:pressed { background: %6; }")
            .arg(QLatin1String(UiTheme::Color::Button), QLatin1String(UiTheme::Color::Border),
                 QLatin1String(UiTheme::Color::TextPrimary), QLatin1String(UiTheme::Color::ButtonHover),
                 QLatin1String(UiTheme::Color::ButtonHoverBorder), QLatin1String(UiTheme::Color::AccentDark));

    const QString sendStyle =
        QStringLiteral("QPushButton {"
                       "  background: %1; border: 1px solid %2; border-radius: 3px;"
                       "  color: %3; font-size: 13px; font-weight: bold;"
                       "}"
                       "QPushButton:hover { background: %4; border-color: %5; }"
                       "QPushButton:disabled { background: %6; color: %7; border-color: %6; }")
            .arg(QLatin1String(UiTheme::Color::Accent), QLatin1String(UiTheme::Color::AccentBright),
                 QLatin1String(UiTheme::Color::PanelDark), QLatin1String(UiTheme::Color::AccentHover),
                 QLatin1String(UiTheme::Color::AccentBright), QLatin1String(UiTheme::Color::Border),
                 QLatin1String(UiTheme::Color::TextMuted));

    auto* clearBtn = new QPushButton(QStringLiteral("Clear"), content);
    auto* bsBtn = new QPushButton(QStringLiteral("⌫"), content);
    m_sendButton = new QPushButton(QStringLiteral("Send"), content);
    m_sendButton->setEnabled(false);

    clearBtn->setFixedSize(52, 32);
    clearBtn->setStyleSheet(ctrlStyle);
    bsBtn->setFixedSize(52, 32);
    bsBtn->setStyleSheet(ctrlStyle);
    m_sendButton->setFixedSize(52, 32);
    m_sendButton->setStyleSheet(sendStyle);

    auto* controlGrid = new QGridLayout;
    controlGrid->setSpacing(4);
    controlGrid->addWidget(clearBtn, 0, 0);
    controlGrid->addWidget(bsBtn, 0, 1);
    controlGrid->addWidget(m_sendButton, 0, 3);
    contentLayout->addLayout(controlGrid);

    connect(clearBtn, &QPushButton::clicked, this,
            [this]()
            {
                m_display->clear();
                m_sendButton->setEnabled(false);
            });

    connect(bsBtn, &QPushButton::clicked, this,
            [this]()
            {
                QString text = m_display->text();
                if (!text.isEmpty())
                {
                    text.chop(1);
                    m_display->setText(text);
                    m_sendButton->setEnabled(!text.isEmpty());
                }
            });

    connect(m_sendButton, &QPushButton::clicked, this,
            [this]()
            {
                const QString digits = m_display->text().toUpper();
                if (!digits.isEmpty())
                {
                    emit sendRequested(digits);
                }
            });
}

void DtmfDialog::appendDigit(const QString& digit)
{
    if (m_display->text().length() < kMaxDtmfDigits)
    {
        m_display->setText(m_display->text() + digit);
        m_sendButton->setEnabled(true);
    }
}

void DtmfDialog::setSendInProgress(bool inProgress)
{
    m_display->setReadOnly(inProgress);
    m_sendButton->setEnabled(!inProgress && !m_display->text().isEmpty());
    for (auto* btn : m_keyButtons)
    {
        btn->setEnabled(!inProgress);
    }
}
