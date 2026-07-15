// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QImage>
#include <QWidget>

class WaterfallCanvas : public QWidget
{
    Q_OBJECT

  public:
    explicit WaterfallCanvas(QWidget* parent = nullptr);

    void setWaterfallImage(const QImage& image);
    void clearDisplay();

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    QImage m_waterfall;
};
