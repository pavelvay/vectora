#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;

class WelcomeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomeWidget(QWidget *parent = nullptr);

    void setRecentFiles(const QStringList &paths);

signals:
    void createRequested();
    void openRequested();
    void sampleRequested();
    void recentRequested(const QString &path);

private:
    QListWidget *m_recentList = nullptr;
    QLabel *m_emptyLabel = nullptr;
};
