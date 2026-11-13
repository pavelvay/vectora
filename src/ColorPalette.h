#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

struct PaletteEntry {
    QColor color;
    QString name;
};

class ColorPalette : public QWidget
{
    Q_OBJECT

public:
    explicit ColorPalette(QWidget *parent = nullptr);

    QSize sizeHint() const override;

signals:
    void colorPicked(const QColor &color, const QString &name);
    void outlineColorPicked(const QColor &color, const QString &name);
    void statusHint(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int indexAt(const QPoint &pos) const;
    QRectF swatchRect(int index) const;

    QVector<PaletteEntry> m_entries;
    int m_hovered = -1;
};
