#include "UpdateChecker.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString releaseApiUrl()
{
    const QString override = qEnvironmentVariable("VECTORA_UPDATE_API");
    if (!override.isEmpty())
        return override;
    return QStringLiteral("https://api.github.com/repos/pavelvay/vectora/releases/latest");
}

QString normalizedVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V')))
        version.remove(0, 1);
    return version;
}

bool isAppImage()
{
    return !qEnvironmentVariable("APPIMAGE").isEmpty();
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

int UpdateChecker::compareVersions(const QString &left, const QString &right)
{
    const QStringList a = normalizedVersion(left).split(QLatin1Char('.'));
    const QStringList b = normalizedVersion(right).split(QLatin1Char('.'));
    const int count = qMax(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        const int av = i < a.size() ? a.at(i).toInt() : 0;
        const int bv = i < b.size() ? b.at(i).toInt() : 0;
        if (av != bv)
            return av < bv ? -1 : 1;
    }
    return 0;
}

QUrl UpdateChecker::chooseAsset(const QVector<UpdateAsset> &assets)
{
#if defined(Q_OS_WIN)
    const auto windowsInstaller = [](const UpdateAsset &asset) {
        return asset.name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
               && asset.name.contains(QStringLiteral("Setup"), Qt::CaseInsensitive);
    };
    for (const UpdateAsset &asset : assets) {
        if (windowsInstaller(asset))
            return asset.url;
    }
    for (const UpdateAsset &asset : assets) {
        if (asset.name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
            return asset.url;
    }
#elif defined(Q_OS_MAC)
    for (const UpdateAsset &asset : assets) {
        if (asset.name.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive))
            return asset.url;
    }
#else
    for (const UpdateAsset &asset : assets) {
        if (asset.name.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)
            && !asset.name.endsWith(QStringLiteral(".zsync"), Qt::CaseInsensitive))
            return asset.url;
    }
    for (const UpdateAsset &asset : assets) {
        if (asset.name.endsWith(QStringLiteral(".deb"), Qt::CaseInsensitive))
            return asset.url;
    }
#endif
    return QUrl();
}

void UpdateChecker::check(bool interactive)
{
    if (m_busy)
        return;
    m_interactive = interactive;
    setBusy(true);

    QNetworkRequest request{QUrl(releaseApiUrl())};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Vectora/%1 (Qt)")
                          .arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, &UpdateChecker::handleReply);
}

void UpdateChecker::handleReply()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    setBusy(false);
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit failed(reply->errorString());
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 && status != 0) {
        emit failed(QStringLiteral("GitHub вернул код %1.").arg(status));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit failed(QStringLiteral("Не удалось прочитать ответ GitHub."));
        return;
    }
    const QJsonObject release = document.object();
    UpdateInfo info;
    info.version = normalizedVersion(
        release.value(QStringLiteral("tag_name")).toString());
    info.notes = release.value(QStringLiteral("body")).toString();
    info.pageUrl = QUrl(release.value(QStringLiteral("html_url")).toString());
    for (const QJsonValue &value :
         release.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = value.toObject();
        UpdateAsset entry;
        entry.name = asset.value(QStringLiteral("name")).toString();
        entry.url = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        entry.size = qint64(asset.value(QStringLiteral("size")).toDouble());
        if (!entry.url.isEmpty())
            info.assets.append(entry);
    }

    if (info.version.isEmpty()) {
        emit failed(QStringLiteral("В релизе нет номера версии."));
        return;
    }
    const QString current = QCoreApplication::applicationVersion();
    if (compareVersions(info.version, current) > 0)
        emit updateAvailable(info);
    else
        emit upToDate(current);
}

void UpdateChecker::setBusy(bool busy)
{
    m_busy = busy;
}

UpdateDialog::UpdateDialog(const UpdateInfo &info, QWidget *parent)
    : QDialog(parent)
    , m_info(info)
{
    setWindowTitle(QStringLiteral("Доступно обновление Vectora"));
    resize(640, 520);

    auto *layout = new QVBoxLayout(this);
    auto *header = new QLabel(
        QStringLiteral("<b>Vectora %1</b> · текущая версия %2")
            .arg(info.version, QCoreApplication::applicationVersion()), this);
    header->setTextFormat(Qt::RichText);
    layout->addWidget(header);

    auto *notes = new QTextBrowser(this);
    notes->setOpenExternalLinks(true);
    notes->setPlainText(info.notes.isEmpty()
                            ? QStringLiteral("Описание изменений не указано.")
                            : info.notes);
    layout->addWidget(notes, 1);

    auto *form = new QFormLayout;
    m_assets = new QComboBox(this);
    for (const UpdateAsset &asset : info.assets)
        m_assets->addItem(asset.name);
    const QUrl preferred = UpdateChecker::chooseAsset(info.assets);
    for (int i = 0; i < info.assets.size(); ++i) {
        if (info.assets.at(i).url == preferred)
            m_assets->setCurrentIndex(i);
    }
    form->addRow(QStringLiteral("Файл обновления"), m_assets);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setVisible(false);
    form->addRow(m_progress);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#8b929d; font-size:11px;"));
    form->addRow(m_status);
    layout->addLayout(form);

    auto *buttons = new QHBoxLayout;
    auto *page = new QPushButton(QStringLiteral("Открыть на GitHub"), this);
    page->setEnabled(info.pageUrl.isValid());
    auto *later = new QPushButton(QStringLiteral("Позже"), this);
    m_download = new QPushButton(QStringLiteral("Скачать и установить"), this);
    m_download->setDefault(true);
    buttons->addWidget(page);
    buttons->addStretch(1);
    buttons->addWidget(later);
    buttons->addWidget(m_download);
    layout->addLayout(buttons);

    if (UpdateChecker::chooseAsset(info.assets).isEmpty()) {
        m_download->setEnabled(false);
        m_status->setText(QStringLiteral(
            "Для этой платформы нет подходящего файла. Откройте страницу релиза."));
    } else if (isAppImage()) {
        m_status->setText(QStringLiteral(
            "Текущий AppImage будет заменён новой версией; после установки "
            "Vectora предложит перезапуск."));
    } else {
        m_status->setText(QStringLiteral(
            "Будет загружен установочный файл; после загрузки откроется "
            "системный установщик."));
    }

    connect(page, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(m_info.pageUrl);
    });
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_download, &QPushButton::clicked, this, &UpdateDialog::startDownload);
}

void UpdateDialog::startDownload()
{
    int index = m_assets->currentIndex();
    if (index < 0 || index >= m_info.assets.size())
        return;
    const UpdateAsset asset = m_info.assets.at(index);
    if (asset.url.isEmpty())
        return;

    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/vectora-update");
    QDir().mkpath(directory);
    m_downloadPath = directory + QLatin1Char('/')
                     + QFileInfo(asset.name).fileName();
    QFile::remove(m_downloadPath);

    m_progress->setVisible(true);
    m_progress->setValue(0);
    m_download->setEnabled(false);
    m_status->setText(QStringLiteral("Загрузка %1…").arg(asset.name));

    QNetworkRequest request(asset.url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Vectora/%1 (Qt)")
                          .arg(QCoreApplication::applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0)
                    m_progress->setValue(int(received * 100 / total));
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            m_status->setText(QStringLiteral("Ошибка загрузки: %1")
                                  .arg(reply->errorString()));
            m_download->setEnabled(true);
            reply->deleteLater();
            return;
        }
        QFile file(m_downloadPath);
        if (!file.open(QIODevice::WriteOnly)) {
            m_status->setText(QStringLiteral("Не удалось сохранить файл обновления."));
            m_download->setEnabled(true);
            reply->deleteLater();
            return;
        }
        file.write(reply->readAll());
        file.close();
        reply->deleteLater();
        m_progress->setValue(100);
        installFile(m_downloadPath);
    });
}

void UpdateDialog::installFile(const QString &path)
{
#if defined(Q_OS_WIN)
    m_status->setText(QStringLiteral("Запуск установщика…"));
    if (!QProcess::startDetached(path, {})) {
        m_status->setText(QStringLiteral("Не удалось запустить установщик: %1").arg(path));
        m_download->setEnabled(true);
    }
#elif defined(Q_OS_MAC)
    m_status->setText(QStringLiteral("Открытие образа обновления…"));
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#else
    const QString current = qEnvironmentVariable("APPIMAGE");
    if (!current.isEmpty()) {
        QFile::setPermissions(path, QFile::permissions(path)
                                        | QFileDevice::ExeOwner
                                        | QFileDevice::ExeGroup
                                        | QFileDevice::ExeOther);
        const QString backup = current + QStringLiteral(".old");
        QFile::remove(backup);
        const bool moved = QFile::rename(current, backup);
        if (moved && QFile::rename(path, current)) {
            m_status->setText(QStringLiteral("Обновление установлено. Перезапустите Vectora."));
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Обновление установлено"),
                QStringLiteral("Новая версия распакована. Перезапустить Vectora сейчас?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (answer == QMessageBox::Yes) {
                QProcess::startDetached(current, {});
                QCoreApplication::quit();
            }
            return;
        }
        if (moved)
            QFile::rename(backup, current);
        m_status->setText(QStringLiteral(
            "Не удалось заменить текущий AppImage. Файл сохранён: %1").arg(path));
    } else {
        if (path.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)) {
            QFile::setPermissions(path, QFile::permissions(path)
                                            | QFileDevice::ExeOwner
                                            | QFileDevice::ExeGroup
                                            | QFileDevice::ExeOther);
        }
        m_status->setText(QStringLiteral("Загрузка завершена: %1").arg(path));
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
#endif
    m_download->setEnabled(true);
}
