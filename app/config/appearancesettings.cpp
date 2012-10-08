/*
  Copyright (C) 2008-2009 by Eike Hein <hein@kde.org>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License or (at your option) version 3 or any later version
  accepted by the membership of KDE e.V. (or its successor appro-
  ved by the membership of KDE e.V.), which shall act as a proxy
  defined in Section 14 of version 3 of the license.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see http://www.gnu.org/licenses/.
*/


#include "appearancesettings.h"
#include "settings.h"
#include "skinlistdelegate.h"

#include <KApplication>
#include <KFileDialog>
#include <KIO/DeleteJob>
#include <KIO/NetAccess>
#include <KMessageBox>
#include <KStandardDirs>
#include <KTar>
#include <KUrl>

#include <KNS3/DownloadDialog>
#include <knewstuff3/downloadmanager.h>

#include <QFile>
#include <QStandardItemModel>

#include <unistd.h>

AppearanceSettings::AppearanceSettings(QWidget* parent) : QWidget(parent)
{
    setupUi(this);

    kcfg_Skin->hide();
    kcfg_SkinInstalledWithKns->hide();

    m_skins = new QStandardItemModel(this);

    m_skinListDelegate = new SkinListDelegate(this);

    skinList->setModel(m_skins);
    skinList->setItemDelegate(m_skinListDelegate);

    connect(skinList->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)),
        this, SLOT(updateSkinSetting()));
    connect(skinList->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)),
        this, SLOT(updateRemoveSkinButton()));
    connect(installButton, SIGNAL(clicked()), this, SLOT(installSkin()));
    connect(removeButton, SIGNAL(clicked()), this, SLOT(removeSelectedSkin()));

    installButton->setIcon(KIcon("folder"));
    removeButton->setIcon(KIcon("edit-delete"));
    ghnsButton->setIcon(KIcon("get-hot-new-stuff"));

#if KDE_IS_VERSION(4, 7, 0)
    m_knsConfigFileName = QLatin1String("yakuake.knsrc");
    m_knsDownloadManager = new KNS3::DownloadManager(m_knsConfigFileName);

    connect(ghnsButton, SIGNAL(clicked()), this, SLOT(getNewSkins()));
#else
    // Hide the GHNS button.
    ghnsButton->setVisible(false);
#endif

    m_selectedSkinId = Settings::skin();

    // Get all local skin directories.
    // One for manually installed skins, one for skins installed
    // through KNS3.
    m_localSkinsDir = KStandardDirs::locateLocal("data", "yakuake/skins/");
    m_knsSkinDir = KStandardDirs::locateLocal("data", "yakuake/kns_skins/");

    // The default skin dir does not have any prefix.
    // These are the skins that were shipped with yakuake.
    m_defaultSkinDir = QLatin1String("yakuake/skins/");

    populateSkinList();
}

AppearanceSettings::~AppearanceSettings()
{
}

void AppearanceSettings::showEvent(QShowEvent* event)
{
    populateSkinList();

    if (skinList->currentIndex().isValid())
        skinList->scrollTo(skinList->currentIndex());

    QWidget::showEvent(event);
}

void AppearanceSettings::populateSkinList()
{
    // Clear the list of skins before getting all installed skins.
    m_skins->clear();

    // Populate the skins which were shipped with yakuake
    // first. Afterwards add all skins which were
    // installed by the user (first the manually ones,
    // then the ones installed via KNS).
    populateSkins(m_defaultSkinDir);
    populateSkins(m_localSkinsDir);
    populateSkins(m_knsSkinDir);

    // Finally sort our skin list.
    m_skins->sort(0);

    updateRemoveSkinButton();
}

void AppearanceSettings::populateSkins(const QString& baseDirectory)
{
    QStringList skinDirs;

    // Filter for title.skin and tabs.skin files in the current skin base directory.
    QString titleFilter = baseDirectory + QLatin1String("/*/title.skin");
    QString tabFilter = baseDirectory + QLatin1String("/*/tabs.skin");

    // Find the title and tab skin files.
    QStringList titleDirs = KGlobal::dirs()->findAllResources("data", titleFilter);
    QStringList tabDirs = KGlobal::dirs()->findAllResources("data", tabFilter);

    QStringListIterator i(titleDirs);

    while (i.hasNext())
    {
        const QString& titleDir = i.next();

        if (tabDirs.contains(titleDir.section('/', 0, -2) + "/tabs.skin"))
            skinDirs << titleDir.section('/', 0, -2);
    }

    if (skinDirs.count() > 0)
    {
        QStringListIterator i(skinDirs);

        while (i.hasNext())
        {
            const QString& skinDir = i.next();
            QString skinId = skinDir.section('/', -1, -1);

            int exists = m_skins->match(m_skins->index(0, 0), SkinId, skinId,
                1, Qt::MatchExactly | Qt::MatchWrap).count();

            if (exists == 0)
            {
                QStandardItem* skin = createSkinItem(skinDir);

                if (!skin)
                    continue;

                m_skins->appendRow(skin);

                if (skin->data(SkinId).toString() == m_selectedSkinId)
                    skinList->setCurrentIndex(skin->index());
            }
        }
    }
}

QStandardItem* AppearanceSettings::createSkinItem(const QString& skinDir)
{
    QString skinId = skinDir.section('/', -1, -1);
    QString titleName, tabName, skinName;
    QString titleAuthor, tabAuthor, skinAuthor;
    QString titleIcon, tabIcon;
    QIcon skinIcon;

    // Check if the skin dir starts with the path where all
    // KNS3 skins are found in.
    bool isKnsSkin = skinDir.startsWith(m_knsSkinDir);

    KConfig titleConfig(skinDir + "/title.skin", KConfig::SimpleConfig);
    KConfigGroup titleDescription = titleConfig.group("Description");

    KConfig tabConfig(skinDir + "/tabs.skin", KConfig::SimpleConfig);
    KConfigGroup tabDescription = tabConfig.group("Description");

    titleName = titleDescription.readEntry("Skin", "");
    titleAuthor = titleDescription.readEntry("Author", "");
    titleIcon = skinDir + titleDescription.readEntry("Icon", "");

    tabName = tabDescription.readEntry("Skin", "");
    tabAuthor = tabDescription.readEntry("Author", "");
    tabIcon = skinDir + tabDescription.readEntry("Icon", "");

    skinName = titleName.isEmpty() ? tabName : titleName;
    skinAuthor = titleAuthor.isEmpty() ? tabAuthor : titleAuthor;
    titleIcon.isEmpty() ? skinIcon.addPixmap(tabIcon) : skinIcon.addPixmap(titleIcon);

    if (skinName.isEmpty() || skinAuthor.isEmpty())
        skinName = skinId;

    if (skinAuthor.isEmpty())
        skinAuthor = i18nc("@item:inlistbox Unknown skin author", "Unknown");

    QStandardItem* skin = new QStandardItem(skinName);

    skin->setData(skinId, SkinId);
    skin->setData(skinDir, SkinDir);
    skin->setData(skinName, SkinName);
    skin->setData(skinAuthor, SkinAuthor);
    skin->setData(skinIcon, SkinIcon);
    skin->setData(isKnsSkin, SkinInstalledWithKns);

    return skin;
}

void AppearanceSettings::updateSkinSetting()
{
    QString skinId = skinList->currentIndex().data(SkinId).toString();

    if (!skinId.isEmpty())
    {
        m_selectedSkinId = skinId;
        kcfg_Skin->setText(skinId);
        kcfg_SkinInstalledWithKns->setChecked(skinList->currentIndex().data(SkinInstalledWithKns).toBool());
    }
}

void AppearanceSettings::resetSelection()
{
    m_selectedSkinId = Settings::skin();

    QModelIndexList skins = m_skins->match(m_skins->index(0, 0), SkinId,
        Settings::skin(), 1, Qt::MatchExactly | Qt::MatchWrap);

    if (skins.count() > 0) skinList->setCurrentIndex(skins.at(0));
}

void AppearanceSettings::installSkin()
{
    QString mimeFilter = "application/x-tar application/x-compressed-tar "
                         "application/x-bzip-compressed-tar application/zip";

    KUrl skinUrl = KFileDialog::getOpenUrl(KUrl(), mimeFilter, parentWidget());

    if (skinUrl.isEmpty()) return;

    if (!KIO::NetAccess::download(skinUrl, m_installSkinFile, KApplication::activeWindow()))
    {
        KMessageBox::error(parentWidget(), KIO::NetAccess::lastErrorString(),
            i18nc("@title:window", "Failed to Download Skin"));

        return;
    }

    QDir skinDir(m_installSkinFile);

    if (!skinDir.exists())
    {
        KIO::ListJob* job = KIO::listRecursive("tar:" + m_installSkinFile, KIO::HideProgressInfo, false);

        connect(job, SIGNAL(entries(KIO::Job*,KIO::UDSEntryList)),
            this, SLOT(listSkinArchive(KIO::Job*,KIO::UDSEntryList)));

        connect(job, SIGNAL(result(KJob*)), this, SLOT(validateSkinArchive(KJob*)));
    }
    else
        failInstall(i18nc("@info", "The installer was given a directory, not a file."));
}

void AppearanceSettings::listSkinArchive(KIO::Job* /* job */, const KIO::UDSEntryList& list)
{
    if (list.count() == 0) return;

    QListIterator<KIO::UDSEntry> i(list);

    while (i.hasNext())
        m_installSkinFileList.append(i.next().stringValue(KIO::UDSEntry::UDS_NAME));
}

void AppearanceSettings::validateSkinArchive(KJob* job)
{
    if (!job->error())
    {
        m_installSkinId = m_installSkinFileList.at(0);

        if (validateSkin(m_installSkinId, m_installSkinFileList))
            checkForExistingSkin();
        else
            failInstall(i18nc("@info", "Unable to locate required files in the skin archive.<nl/><nl/>The archive appears to be invalid."));
    }
    else
        failInstall(i18nc("@info", "Unable to list the skin archive contents.") + "\n\n" + job->errorString());
}

bool AppearanceSettings::validateSkin(const QString &skinId, const QStringList& fileList)
{
    bool titleFileFound = false;
    bool tabsFileFound = false;
    QString titleFileName = skinId + "/title.skin";
    QString tabsFileName = skinId + "/tabs.skin";

    foreach (const QString& fileName, fileList)
    {
        if (fileName.endsWith(titleFileName))
        {
            titleFileFound = true;
        }
        else if (fileName.endsWith(tabsFileName))
        {
            tabsFileFound = true;
        }
    }

    return titleFileFound && tabsFileFound;
}

void AppearanceSettings::checkForExistingSkin()
{
    QModelIndexList skins = m_skins->match(m_skins->index(0, 0), SkinId,
        m_installSkinId, 1, Qt::MatchExactly | Qt::MatchWrap);

    int exists = skins.count();

    if (exists > 0)
    {
        QString skinDir = skins.at(0).data(SkinDir).toString();
        QFile skin(skinDir + "/titles.skin");

        if (!skin.open(QIODevice::ReadWrite))
        {
            failInstall(i18nc("@info", "This skin appears to be already installed and you lack the required permissions to overwrite it."));
        }
        else
        {
            skin.close();

            int remove = KMessageBox::warningContinueCancel(parentWidget(),
                i18nc("@info", "This skin appears to be already installed. Do you want to overwrite it?"),
                i18nc("@title:window", "Skin Already Exists"),
                KGuiItem(i18nc("@action:button", "Reinstall Skin")));

            if (remove == KMessageBox::Continue)
            {
                unlink(QFile::encodeName(skinDir));
                KIO::DeleteJob* job = KIO::del(KUrl(skinDir), KIO::HideProgressInfo);
                connect(job, SIGNAL(result(KJob*)), this, SLOT(installSkinArchive(KJob*)));
            }
            else
                cleanupAfterInstall();
        }
    }
    else
        installSkinArchive();
}

void AppearanceSettings::installSkinArchive(KJob* deleteJob)
{
    if (deleteJob && deleteJob->error())
    {
        KMessageBox::error(parentWidget(), deleteJob->errorString(), i18nc("@title:Window", "Could Not Delete Skin"));

        return;
    }

    KTar skinArchive(m_installSkinFile);

    if (skinArchive.open(QIODevice::ReadOnly))
    {
        const KArchiveDirectory* skinDir = skinArchive.directory();
        skinDir->copyTo(m_localSkinsDir);
        skinArchive.close();

        populateSkinList();

        if (Settings::skin() == m_installSkinId)
            emit settingsChanged();

        cleanupAfterInstall();
    }
    else
        failInstall(i18nc("@info", "The skin archive file could not be opened."));
}

void AppearanceSettings::failInstall(const QString& error)
{
    KMessageBox::error(parentWidget(), error, i18nc("@title:window", "Cannot Install Skin"));

    cleanupAfterInstall();
}

void AppearanceSettings::cleanupAfterInstall()
{
    KIO::NetAccess::removeTempFile(m_installSkinFile);
    m_installSkinId.clear();
    m_installSkinFile.clear();
    m_installSkinFileList.clear();
}

void AppearanceSettings::updateRemoveSkinButton()
{
    if (m_skins->rowCount() <= 1)
    {
        removeButton->setEnabled(false);
        return;
    }

    QString skinDir;

    QVariant value = skinList->currentIndex().data(SkinDir);
    if (value.isValid())
        skinDir = value.toString();

    value = skinList->currentIndex().data(SkinInstalledWithKns);
    bool isKnsSkin = value.toBool();

    // We don't allow the user to remove the default skin
    // or any skin which was installed through KNS3.
    if (skinDir.isEmpty() || isKnsSkin)
    {
        removeButton->setEnabled(false);
        return;
    }

    QFile titleSkin(skinDir + "/title.skin");

    if (!titleSkin.open(QIODevice::ReadWrite))
        removeButton->setEnabled(false);
    else
        removeButton->setEnabled(true);

    titleSkin.close();
}

void AppearanceSettings::removeSelectedSkin()
{
    if (m_skins->rowCount() <= 1) return;

    QString skinId = skinList->currentIndex().data(SkinId).toString();
    QString skinDir = skinList->currentIndex().data(SkinDir).toString();
    QString skinName = skinList->currentIndex().data(SkinName).toString();
    QString skinAuthor = skinList->currentIndex().data(SkinAuthor).toString();

    if (skinDir.isEmpty()) return;

    int remove = KMessageBox::warningContinueCancel(parentWidget(),
            i18nc("@info", "Do you want to remove \"%1\" by %2?", skinName, skinAuthor),
            i18nc("@title:window", "Remove Skin"),
            KStandardGuiItem::del());

    if (remove == KMessageBox::Continue)
    {
        unlink(QFile::encodeName(skinDir));

        bool deleted = KIO::NetAccess::del(KUrl(skinDir), KApplication::activeWindow());

        if (deleted)
        {
            if (skinId == Settings::skin())
            {
                Settings::setSkin("default");
                Settings::setSkinInstalledWithKns(false);
                Settings::self()->writeConfig();
                emit settingsChanged();
            }

            resetSelection();
            populateSkinList();
        }
        else
            KMessageBox::error(parentWidget(), i18nc("@info", "Could not remove skin \"%1\".", skinName));
    }
}

#if KDE_IS_VERSION(4, 7, 0)

QStringList AppearanceSettings::extractKnsSkinIds(const QStringList& fileList)
{
    QStringList skinIdList;

    foreach (const QString& file, fileList)
    {
        // We only care about files/directories which are subdirectories of our KNS skins dir.
        if (file.startsWith(m_knsSkinDir, Qt::CaseInsensitive))
        {
            // Get the relative filename (this removes the KNS install dir from the filename).
            QString relativeName = QString(file).remove(m_knsSkinDir, Qt::CaseInsensitive);

            // Get everything before the first slash - that should be our skins ID.
            QString skinId = relativeName.section('/', 0, QString::SectionSkipEmpty);

            // Skip all other entries in the file list if we found what we were searching for.
            if (!skinId.isEmpty())
            {
                // First remove all remaining slashes (as there could be leading or trailing ones).
                skinId = skinId.replace('/', QString());

                // Don't insert duplicate entries.
                if (skinIdList.contains(skinId))
                {
                    skinIdList.append(skinId);
                }
            }
        }
    }

    return skinIdList;
}

void AppearanceSettings::getNewSkins()
{
    KNS3::DownloadDialog dialog(m_knsConfigFileName, this);
    dialog.exec();

    if (!dialog.installedEntries().empty())
    {
        quint32 invalidEntryCount = 0;
        QString invalidSkinText;

        foreach (const KNS3::Entry &entry, dialog.installedEntries())
        {
            bool isValid = true;
            QStringList skinIdList = extractKnsSkinIds(entry.installedFiles());

            // Validate all skin IDs as each archive can contain multiple skins.
            foreach (const QString& skinId, skinIdList)
            {
                // Validate the current skin.
                if (!validateSkin(skinId, entry.installedFiles()))
                {
                    kDebug() << "skinId '" << skinId << "' is invalid "
                             << "(it's either missing the 'title.skin' or 'tabs.skin' file).";
                    isValid = false;
                }
            }

            // We'll add an error message for the whole KNS entry if
            // the current skin is marked as invalid.
            // We should not do this per skin as the user does not know that
            // there are more skins inside one archive.
            if (!isValid)
            {
                invalidEntryCount++;

                // The user needs to know the name of the skin which
                // was removed.
                invalidSkinText += QString("<li>%1</li>").arg(entry.name());

                // Then remove the skin.
                m_knsDownloadManager->uninstallEntry(entry);
            }
        }

        // Are there any invalid entries?
        if (invalidEntryCount > 0)
        {
            failInstall(i18ncp("@info",
                               "The following skin is missing required files. Thus it was removed:<ul>%2</ul>",
                               "The following skins are missing required files. Thus they were removed:<ul>%2</ul>",
                               invalidEntryCount,
                               invalidSkinText));
        }
    }

    if (!dialog.changedEntries().isEmpty())
    {
        // Reset the selection in case the currently selected
        // skin was removed.
        resetSelection();

        // Re-populate the list of skins if the user changed something.
        populateSkinList();
    }
}

#else

void AppearanceSettings::getNewSkins()
{
    // This shouldn't be possible since the connect() for this slot
    // is protected by the same if.
    Q_ASSERT(false);
}

#endif
