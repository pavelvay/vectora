#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

struct UpdateAsset {
    QString name;
    QUrl url;
    qint64 size = 0;
};

struct UpdateInfo {
    QString version;
    QString notes;
    QUrl pageUrl;
    QVector<UpdateAsset> assets;
};

// Checks the public GitHub release feed for a newer Vectora build.
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    void check(bool interactive);

    // Pure helpers, shared with the tests.
    static int compareVersions(const QString &left, const QString &right);
    static QUrl chooseAsset(const QVector<UpdateAsset> &assets);

signals:
    void updateAvailable(const UpdateInfo &info);
    void upToDate(const QString &version);
    void failed(const QString &error);

private:
    void handleReply();
    void handleFallbackReply();
    void handleManifestReply();
    void requestManifest();
    void requestLatestRelease();
    void deliver(const UpdateInfo &info);
    void setBusy(bool busy);

    QNetworkAccessManager m_network;
    bool m_interactive = false;
    bool m_busy = false;
    int m_fallbackStage = 0;
    QString m_lastError;
};

// Shows release notes, lets the user download the matching package and starts
// the platform installer (or replaces the running AppImage).
class UpdateDialog : public QDialog
{
    Q_OBJECT

public:
    UpdateDialog(const UpdateInfo &info, QWidget *parent = nullptr);

private:
    void startDownload();
    void onDownloadFinished();
    void installFile(const QString &path);

    UpdateInfo m_info;
    QNetworkAccessManager m_network;
    QComboBox *m_assets = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_download = nullptr;
    QString m_downloadPath;
};
