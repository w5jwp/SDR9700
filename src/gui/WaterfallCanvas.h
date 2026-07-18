// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QImage>
#include <QWidget>

class WaterfallCanvas : public QWidget
{
    Q_OBJECT

  public:
    explicit WaterfallCanvas(QWidget* parent = nullptr);

    void setWaterfallImageSource(const QImage* image);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    // WaterfallController owns this image and outlives the canvas within
    // SpectrumScopeDisplay. Keeping a non-owning source avoids QImage
    // copy-on-write detaching the complete waterfall on every rendered row.
    const QImage* m_waterfall{nullptr};
};
