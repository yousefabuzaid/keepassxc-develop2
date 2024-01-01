/*
 *  Copyright (C) 2018 KeePassXC Team <team@keepassxc.org>
 *  Copyright (C) 2010 Felix Geyer <debfx@fobos.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Database.h"

#include "core/AsyncTask.h"
#include "core/FileWatcher.h"
#include "core/Group.h"
#include "format/KdbxXmlReader.h"
#include "format/KeePass2Reader.h"
#include "format/KeePass2Writer.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTimer>

QHash<QUuid, QPointer<Database>> Database::s_uuidMap;

Database::Database()
    : m_metadata(new Metadata(this))
    , m_data()
    , m_rootGroup(nullptr)
    , m_fileWatcher(new FileWatcher(this))
    , m_uuid(QUuid::createUuid())
{
    // setup modified timer
    m_modifiedTimer.setSingleShot(true);
    connect(this, &Database::emitModifiedChanged, this, [this](bool value) {
        if (!value) {
            stopModifiedTimer();
        }
    });
    connect(&m_modifiedTimer, &QTimer::timeout, this, &Database::emitModified);

    // other signals
    connect(m_metadata, &Metadata::modified, this, &Database::markAsModified);
    connect(this, &Database::databaseOpened, this, [this]() {
        updateCommonUsernames();
        updateTagList();
    });
    connect(this, &Database::modified, this, [this] { updateTagList(); });
    connect(this, &Database::databaseSaved, this, [this]() { updateCommonUsernames(); });
    connect(m_fileWatcher, &FileWatcher::fileChanged, this, &Database::databaseFileChanged);

    // static uuid map
    s_uuidMap.insert(m_uuid, this);

    // block modified signal and set root group
    setEmitModified(false);

    setRootGroup(new Group());
    rootGroup()->setUuid(QUuid::createUuid());
    rootGroup()->setName(tr("Passwords", "Root group name"));

    m_modified = false;
    setEmitModified(true);
}

Database::Database(const QString& filePath)
    : Database()
{
    setFilePath(filePath);
}

Database::~Database()
{
    releaseData();
}

QUuid Database::uuid() const
{
    return m_uuid;
}

/**
 * Open the database from a previously specified file.
 * Unless `readOnly` is set to false, the database will be opened in
 * read-write mode and fall back to read-only if that is not possible.
 *
 * @param key composite key for unlocking the database
 * @param error error message in case of failure
 * @return true on success
 */
bool Database::open(QSharedPointer<const CompositeKey> key, QString* error)
{
    Q_ASSERT(!m_data.filePath.isEmpty());
    if (m_data.filePath.isEmpty()) {
        return false;
    }
    return open(m_data.filePath, std::move(key), error);
}

/**
 * Open the database from a file.
 * Unless `readOnly` is set to false, the database will be opened in
 * read-write mode and fall back to read-only if that is not possible.
 *
 * If key is provided as null, only headers will be read.
 *
 * @param filePath path to the file
 * @param key composite key for unlocking the database
 * @param error error message in case of failure
 * @return true on success
 */
bool Database::open(const QString& filePath, QSharedPointer<const CompositeKey> key, QString* error)
{
    QFile dbFile(filePath);
    if (!dbFile.exists()) {
        if (error) {
            *error = tr("File %1 does not exist.").arg(filePath);
        }
        return false;
    }

    // Don't autodetect read-only mode, as it triggers an upstream bug.
    // See https://github.com/keepassxreboot/keepassxc/issues/803
    // if (!readOnly && !dbFile.open(QIODevice::ReadWrite)) {
    //     readOnly = true;
    // }
    //
    // if (!dbFile.isOpen() && !dbFile.open(QIODevice::ReadOnly)) {
    if (!dbFile.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = tr("Unable to open file %1.").arg(filePath);
        }
        return false;
    }

    setEmitModified(false);

    KeePass2Reader reader;
    if (!reader.readDatabase(&dbFile, std::move(key), this)) {
        if (error) {
            *error = tr("Error while reading the database: %1").arg(reader.errorString());
        }
        return false;
    }

    setFilePath(filePath);
    dbFile.close();

    markAsClean();

    emit databaseOpened();
    m_fileWatcher->start(canonicalFilePath(), 30, 1);
    setEmitModified(true);

    return true;
}

/**
 * KDBX format version.
 */
quint32 Database::formatVersion() const
{
    return m_data.formatVersion;
}

void Database::setFormatVersion(quint32 version)
{
    m_data.formatVersion = version;
}

/**
 * Whether the KDBX minor version is greater than the newest supported.
 */
bool Database::hasMinorVersionMismatch() const
{
    return m_data.formatVersion > KeePass2::FILE_VERSION_MAX;
}

bool Database::isSaving()
{
    bool locked = m_saveMutex.tryLock();
    if (locked) {
        m_saveMutex.unlock();
    }
    return !locked;
}

/**
 * Save the database to the current file path. It is an error to call this function
 * if no file path has been defined.
 *
 * @param error error message in case of failure
 * @param atomic Use atomic file transactions
 * @param backupFilePath Absolute file path to write the backup file to. Pass an empty QString to disable backup.
 * @return true on success
 */
bool Database::save(SaveAction action, const QString& backupFilePath, QString* error)
{
    Q_ASSERT(!m_data.filePath.isEmpty());
    if (m_data.filePath.isEmpty()) {
        if (error) {
            *error = tr("Could not save, database does not point to a valid file.");
        }
        return false;
    }

    return saveAs(m_data.filePath, action, backupFilePath, error);
}

/**
 * Save the database to a specific file.
 *
 * If atomic is false, this function uses QTemporaryFile instead of QSaveFile
 * due to a bug in Qt (https://bugreports.qt.io/browse/QTBUG-57299) that may
 * prevent the QSaveFile from renaming itself when using Dropbox, Google Drive,
 * or OneDrive.
 *
 * The risk in using QTemporaryFile is that the rename function is not atomic
 * and may result in loss of data if there is a crash or power loss at the
 * wrong moment.
 *
 * @param filePath Absolute path of the file to save
 * @param error error message in case of failure
 * @param atomic Use atomic file transactions
 * @param backupFilePath Absolute path to the location where the backup should be stored. Passing an empty string
 * disables backup.
 * @return true on success
 */
bool Database::saveAs(const QString& filePath, SaveAction action, const QString& backupFilePath, QString* error)
{
    // Disallow overlapping save operations
    if (isSaving()) {
        if (error) {
            *error = tr("Database save is already in progress.");
        }
        return false;
    }

    // Never save an uninitialized database
    if (!isInitialized()) {
        if (error) {
            *error = tr("Could not save, database has not been initialized!");
        }
        return false;
    }

    if (filePath == m_data.filePath) {
        // Fail-safe check to make sure we don't overwrite underlying file changes
        // that have not yet triggered a file reload/merge operation.
        if (!m_fileWatcher->hasSameFileChecksum()) {
            if (error) {
                *error = tr("Database file has unmerged changes.");
            }
            return false;
        }
    }

    // Clear read-only flag
    m_fileWatcher->stop();

    // Prevent destructive operations while saving
    QMutexLocker locker(&m_saveMutex);

    QFileInfo fileInfo(filePath);
    auto realFilePath = fileInfo.exists() ? fileInfo.canonicalFilePath() : fileInfo.absoluteFilePath();
    bool isNewFile = !QFile::exists(realFilePath);
    bool ok = AsyncTask::runAndWaitForFuture([&] { return performSave(realFilePath, action, backupFilePath, error); });
    if (ok) {
        setFilePath(filePath);
        markAsClean();
        if (isNewFile) {
            QFile::setPermissions(realFilePath, QFile::ReadUser | QFile::WriteUser);
        }
        m_fileWatcher->start(realFilePath, 30, 1);
    } else {
        // Saving failed, don't rewatch file since it does not represent our database
        markAsModified();
    }

    return ok;
}

bool Database::performSave(const QString& filePath, SaveAction action, const QString& backupFilePath, QString* error)
{
    if (!backupFilePath.isNull()) {
        backupDatabase(filePath, backupFilePath);
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QFileInfo info(filePath);
    auto createTime = info.exists() ? info.birthTime() : QDateTime::currentDateTime();
#endif

    switch (action) {
    case Atomic: {
        QSaveFile saveFile(filePath);
        if (saveFile.open(QIODevice::WriteOnly)) {
            // write the database to the file
            if (!writeDatabase(&saveFile, error)) {
                return false;
            }

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
            // Retain original creation time
            saveFile.setFileTime(createTime, QFile::FileBirthTime);
#endif

            if (saveFile.commit()) {
                // successfully saved database file
                return true;
            }
        }

        if (error) {
            *error = saveFile.errorString();
        }
        break;
    }
    case TempFile: {
        QTemporaryFile tempFile;
        if (tempFile.open()) {
            // write the database to the file
            if (!writeDatabase(&tempFile, error)) {
                return false;
            }
            tempFile.close(); // flush to disk

            // Delete the original db and move the temp file in place
            auto perms = QFile::permissions(filePath);
            QFile::remove(filePath);

            // Note: call into the QFile rename instead of QTemporaryFile
            // due to an undocumented difference in how the function handles
            // errors. This prevents errors when saving across file systems.
            if (tempFile.QFile::rename(filePath)) {
                // successfully saved the database
                tempFile.setAutoRemove(false);
                QFile::setPermissions(filePath, perms);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
                // Retain original creation time
                tempFile.setFileTime(createTime, QFile::FileBirthTime);
#endif
                return true;
            } else if (backupFilePath.isEmpty() || !restoreDatabase(filePath, backupFilePath)) {
                // Failed to copy new database in place, and
                // failed to restore from backup or backups disabled
                tempFile.setAutoRemove(false);
                if (error) {
                    *error = tr("%1\nBackup database located at %2").arg(tempFile.errorString(), tempFile.fileName());
                }
                return false;
            }
        }

        if (error) {
            *error = tempFile.errorString();
        }
        break;
    }
    case DirectWrite: {
        // Open the original database file for direct-write
        QFile dbFile(filePath);
        if (dbFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (!writeDatabase(&dbFile, error)) {
                return false;
            }
            dbFile.close();
            return true;
        }
        if (error) {
            *error = dbFile.errorString();
        }
        break;
    }
    }

    // Saving failed
    return false;
}

bool Database::writeDatabase(QIODevice* device, QString* error)
{
    PasswordKey oldTransformedKey;
    if (m_data.key->isEmpty()) {
        oldTransformedKey.setRawKey(m_data.transformedDatabaseKey->rawKey());
    }

    KeePass2Writer writer;
    setEmitModified(false);
    writer.writeDatabase(device, this);
    setEmitModified(true);

    if (writer.hasError()) {
        if (error) {
            *error = writer.errorString();
        }
        return false;
    }

    QByteArray newKey = m_data.transformedDatabaseKey->rawKey();
    Q_ASSERT(!newKey.isEmpty());
    Q_ASSERT(newKey != oldTransformedKey.rawKey());
    if (newKey.isEmpty() || newKey == oldTransformedKey.rawKey()) {
        if (error) {
            *error = tr("Key not transformed. This is a bug, please report it to the developers.");
        }
        return false;
    }

    return true;
}

bool Database::extract(QByteArray& xmlOutput, QString* error)
{
    KeePass2Writer writer;
    writer.extractDatabase(this, xmlOutput);
    if (writer.hasError()) {
        if (error) {
            *error = writer.errorString();
        }
        return false;
    }

    return true;
}

bool Database::import(const QString& xmlExportPath, QString* error)
{
    KdbxXmlReader reader(KeePass2::FILE_VERSION_4);
    QFile file(xmlExportPath);
    file.open(QIODevice::ReadOnly);

    reader.readDatabase(&file, this);

    if (reader.hasError()) {
        if (error) {
            *error = reader.errorString();
        }
        return false;
    }

    return true;
}

/**
 * Release all stored group, entry, and meta data of this database.
 *
 * Call this method to ensure all data is cleared even if valid
 * pointers to this Database object are still being held.
 *
 * A previously reparented root group will not be freed.
 */

void Database::releaseData()
{
    // Prevent data release while saving
    Q_ASSERT(!isSaving());
    QMutexLocker locker(&m_saveMutex);

    if (m_modified) {
        emit databaseDiscarded();
    }

    setEmitModified(false);
    m_modified = false;

    s_uuidMap.remove(m_uuid);
    m_uuid = QUuid();

    m_data.clear();
    m_metadata->clear();

    auto oldGroup = rootGroup();
    setRootGroup(new Group());
    // explicitly delete old group, otherwise it is only deleted when the database object is destructed
    delete oldGroup;

    m_fileWatcher->stop();

    m_deletedObjects.clear();
    m_commonUsernames.clear();
    m_tagList.clear();
}

/**
 * Remove the old backup and replace it with a new one. Backup name is taken from destinationFilePath.
 * Non-existing parent directories will be created automatically.
 *
 * @param filePath Path to the file to backup
 * @param destinationFilePath Path to the backup destination file
 * @return true on success
 */
bool Database::backupDatabase(const QString& filePath, const QString& destinationFilePath)
{
    // Ensure that the path to write to actually exists
    auto parentDirectory = QFileInfo(destinationFilePath).absoluteDir();
    if (!parentDirectory.exists()) {
        if (!QDir().mkpath(parentDirectory.absolutePath())) {
            return false;
        }
    }
    auto perms = QFile::permissions(filePath);
    QFile::remove(destinationFilePath);
    bool res = QFile::copy(filePath, destinationFilePath);
    QFile::setPermissions(destinationFilePath, perms);
    return res;
}

/**
 * Restores the database file from the backup file with
 * name <filename>.old.<extension> to filePath. This will
 * overwrite the existing file!
 *
 * @param filePath Path to the file to restore
 * @return true on success
 */
bool Database::restoreDatabase(const QString& filePath, const QString& fromBackupFilePath)
{
    auto perms = QFile::permissions(filePath);
    // Only try to restore if the backup file actually exists
    if (QFile::exists(fromBackupFilePath)) {
        QFile::remove(filePath);
        if (QFile::copy(fromBackupFilePath, filePath)) {
            return QFile::setPermissions(filePath, perms);
        }
    }
    return false;
}

/**
 * Returns true if the database key exists, has subkeys, and the
 * root group exists
 *
 * @return true if database has been fully initialized
 */
bool Database::isInitialized() const
{
    return m_data.key && !m_data.key->isEmpty() && m_rootGroup;
}

Group* Database::rootGroup()
{
    return m_rootGroup;
}

const Group* Database::rootGroup() const
{
    return m_rootGroup;
}

/**
 * Sets group as the root group and takes ownership of it.
 * Warning: Be careful when calling this method as it doesn't
 *          emit any notifications so e.g. models aren't updated.
 *          The caller is responsible for cleaning up the previous
            root group.
 */
void Database::setRootGroup(Group* group)
{
    Q_ASSERT(group);

    if (isInitialized() && isModified()) {
        emit databaseDiscarded();
    }

    m_rootGroup = group;
    m_rootGroup->setParent(this);
}

Metadata* Database::metadata()
{
    return m_metadata;
}

const Metadata* Database::metadata() const
{
    return m_metadata;
}

/**
 * Returns the original file path that was provided for
 * this database. This path may not exist, may contain
 * unresolved symlinks, or have malformed slashes.
 *
 * @return original file path
 */
QString Database::filePath() const
{
    return m_data.filePath;
}

/**
 * Returns the canonical file path of this databases'
 * set file path. This returns an empty string if the
 * file does not exist or cannot be resolved.
 *
 * @return canonical file path
 */
QString Database::canonicalFilePath() const
{
    QFileInfo fileInfo(m_data.filePath);
    return fileInfo.canonicalFilePath();
}

void Database::setFilePath(const QString& filePath)
{
    if (filePath != m_data.filePath) {
        QString oldPath = m_data.filePath;
        m_data.filePath = filePath;
        // Don't watch for changes until the next open or save operation
        m_fileWatcher->stop();
        emit filePathChanged(oldPath, filePath);
    }
}

QList<DeletedObject> Database::deletedObjects()
{
    return m_deletedObjects;
}

const QList<DeletedObject>& Database::deletedObjects() const
{
    return m_deletedObjects;
}

bool Database::containsDeletedObject(const QUuid& uuid) const
{
    for (const DeletedObject& currentObject : m_deletedObjects) {
        if (currentObject.uuid == uuid) {
            return true;
        }
    }
    return false;
}

bool Database::containsDeletedObject(const DeletedObject& object) const
{
    for (const DeletedObject& currentObject : m_deletedObjects) {
        if (currentObject.uuid == object.uuid) {
            return true;
        }
    }
    return false;
}

void Database::setDeletedObjects(const QList<DeletedObject>& delObjs)
{
    if (m_deletedObjects == delObjs) {
        return;
    }
    m_deletedObjects = delObjs;
}

void Database::addDeletedObject(const DeletedObject& delObj)
{
    Q_ASSERT(delObj.deletionTime.timeSpec() == Qt::UTC);
    m_deletedObjects.append(delObj);
}

void Database::addDeletedObject(const QUuid& uuid)
{
    DeletedObject delObj;
    delObj.deletionTime = Clock::currentDateTimeUtc();
    delObj.uuid = uuid;

    addDeletedObject(delObj);
}

const QStringList& Database::commonUsernames() const
{
    return m_commonUsernames;
}

const QStringList& Database::tagList() const
{
    return m_tagList;
}

void Database::updateCommonUsernames(int topN)
{
    m_commonUsernames.clear();
    m_commonUsernames.append(rootGroup()->usernamesRecursive(topN));
}

void Database::updateTagList()
{
    m_tagList.clear();
    if (!m_rootGroup) {
        emit tagListUpdated();
        return;
    }

    // Search groups recursively looking for tags
    // Use a set to prevent adding duplicates
    QSet<QString> tagSet;
    for (auto entry : m_rootGroup->entriesRecursive()) {
        if (!entry->isRecycled()) {
            for (auto tag : entry->tagList()) {
                tagSet.insert(tag);
            }
        }
    }

    m_tagList = tagSet.toList();
    m_tagList.sort();
    emit tagListUpdated();
}

void Database::removeTag(const QString& tag)
{
    if (!m_rootGroup) {
        return;
    }

    for (auto entry : m_rootGroup->entriesRecursive()) {
        entry->removeTag(tag);
    }
}

const QUuid& Database::cipher() const
{
    return m_data.cipher;
}

Database::CompressionAlgorithm Database::compressionAlgorithm() const
{
    return m_data.compressionAlgorithm;
}

QByteArray Database::transformedDatabaseKey() const
{
    return m_data.transformedDatabaseKey->rawKey();
}

QByteArray Database::challengeResponseKey() const
{
    return m_data.challengeResponseKey->rawKey();
}

bool Database::challengeMasterSeed(const QByteArray& masterSeed)
{
    m_keyError.clear();
    if (m_data.key) {
        m_data.masterSeed->setRawKey(masterSeed);
        QByteArray response;
        bool ok = m_data.key->challenge(masterSeed, response, &m_keyError);
        if (ok && !response.isEmpty()) {
            m_data.challengeResponseKey->setRawKey(response);
        } else if (ok && response.isEmpty()) {
            // no CR key present, make sure buffer is empty
            m_data.challengeResponseKey.reset(new PasswordKey);
        }
        return ok;
    }
    return false;
}

void Database::setCipher(const QUuid& cipher)
{
    Q_ASSERT(!cipher.isNull());

    m_data.cipher = cipher;
}

void Database::setCompressionAlgorithm(Database::CompressionAlgorithm algo)
{
    Q_ASSERT(static_cast<quint32>(algo) <= CompressionAlgorithmMax);

    m_data.compressionAlgorithm = algo;
}

/**
 * Set and transform a new encryption key.
 *
 * @param key key to set and transform or nullptr to reset the key
 * @param updateChangedTime true to update database change time
 * @param updateTransformSalt true to update the transform salt
 * @param transformKey trigger the KDF after setting the key
 * @return true on success
 */
bool Database::setKey(const QSharedPointer<const CompositeKey>& key,
                      bool updateChangedTime,
                      bool updateTransformSalt,
                      bool transformKey)
{
    m_keyError.clear();

    if (!key) {
        m_data.key.reset();
        m_data.transformedDatabaseKey.reset(new PasswordKey());
        return true;
    }

    if (updateTransformSalt) {
        m_data.kdf->randomizeSeed();
        Q_ASSERT(!m_data.kdf->seed().isEmpty());
    }

    PasswordKey oldTransformedDatabaseKey;
    if (m_data.key && !m_data.key->isEmpty()) {
        oldTransformedDatabaseKey.setRawKey(m_data.transformedDatabaseKey->rawKey());
    }

    QByteArray transformedDatabaseKey;

    if (!transformKey) {
        transformedDatabaseKey = QByteArray(oldTransformedDatabaseKey.rawKey());
    } else if (!key->transform(*m_data.kdf, transformedDatabaseKey, &m_keyError)) {
        return false;
    }

    m_data.key = key;
    if (!transformedDatabaseKey.isEmpty()) {
        m_data.transformedDatabaseKey->setRawKey(transformedDatabaseKey);
    }
    if (updateChangedTime) {
        m_metadata->setDatabaseKeyChanged(Clock::currentDateTimeUtc());
    }

    if (oldTransformedDatabaseKey.rawKey() != m_data.transformedDatabaseKey->rawKey()) {
        markAsModified();
    }

    return true;
}

QString Database::keyError()
{
    return m_keyError;
}

QVariantMap& Database::publicCustomData()
{
    return m_data.publicCustomData;
}

const QVariantMap& Database::publicCustomData() const
{
    return m_data.publicCustomData;
}

void Database::setPublicCustomData(const QVariantMap& customData)
{
    m_data.publicCustomData = customData;
}

void Database::createRecycleBin()
{
    auto recycleBin = new Group();
    recycleBin->setUuid(QUuid::createUuid());
    recycleBin->setParent(rootGroup());
    recycleBin->setName(tr("Recycle Bin"));
    recycleBin->setIcon(Group::RecycleBinIconNumber);
    recycleBin->setSearchingEnabled(Group::Disable);
    recycleBin->setAutoTypeEnabled(Group::Disable);

    m_metadata->setRecycleBin(recycleBin);
}

void Database::recycleEntry(Entry* entry)
{
    if (m_metadata->recycleBinEnabled()) {
        if (!m_metadata->recycleBin()) {
            createRecycleBin();
        }
        entry->setGroup(metadata()->recycleBin());
    } else {
        delete entry;
    }
}

void Database::recycleGroup(Group* group)
{
    if (m_metadata->recycleBinEnabled()) {
        if (!m_metadata->recycleBin()) {
            createRecycleBin();
        }
        group->setParent(metadata()->recycleBin());
    } else {
        delete group;
    }
}

void Database::emptyRecycleBin()
{
    if (m_metadata->recycleBinEnabled() && m_metadata->recycleBin()) {
        // destroying direct entries of the recycle bin
        QList<Entry*> subEntries = m_metadata->recycleBin()->entries();
        for (Entry* entry : subEntries) {
            delete entry;
        }
        // destroying direct subgroups of the recycle bin
        QList<Group*> subGroups = m_metadata->recycleBin()->children();
        for (Group* group : subGroups) {
            delete group;
        }
    }
}

bool Database::isModified() const
{
    return m_modified;
}

bool Database::hasNonDataChanges() const
{
    return m_hasNonDataChange;
}

void Database::markAsModified()
{
    m_modified = true;
    if (modifiedSignalEnabled() && !m_modifiedTimer.isActive()) {
        // Small time delay prevents numerous consecutive saves due to repeated signals
        startModifiedTimer();
    }
}

void Database::markAsClean()
{
    bool emitSignal = m_modified;
    m_modified = false;
    stopModifiedTimer();
    m_hasNonDataChange = false;
    if (emitSignal) {
        emit databaseSaved();
    }
}

void Database::markNonDataChange()
{
    m_hasNonDataChange = true;
    emit databaseNonDataChanged();
}

/**
 * @param uuid UUID of the database
 * @return pointer to the database or nullptr if no such database exists
 */
Database* Database::databaseByUuid(const QUuid& uuid)
{
    return s_uuidMap.value(uuid, nullptr);
}

QSharedPointer<const CompositeKey> Database::key() const
{
    return m_data.key;
}

QSharedPointer<Kdf> Database::kdf() const
{
    return m_data.kdf;
}

void Database::setKdf(QSharedPointer<Kdf> kdf)
{
    m_data.kdf = std::move(kdf);
    setFormatVersion(KeePass2Writer::kdbxVersionRequired(this, true, m_data.kdf.isNull()));
}

bool Database::changeKdf(const QSharedPointer<Kdf>& kdf)
{
    kdf->randomizeSeed();
    QByteArray transformedDatabaseKey;
    if (!m_data.key) {
        m_data.key = QSharedPointer<CompositeKey>::create();
    }
    if (!m_data.key->transform(*kdf, transformedDatabaseKey)) {
        return false;
    }

    setKdf(kdf);
    m_data.transformedDatabaseKey->setRawKey(transformedDatabaseKey);
    markAsModified();

    return true;
}

// Prevent warning about QTimer not allowed to be started/stopped from other thread
void Database::startModifiedTimer()
{
    QMetaObject::invokeMethod(&m_modifiedTimer, "start", Q_ARG(int, 150));
}

// Prevent warning about QTimer not allowed to be started/stopped from other thread
void Database::stopModifiedTimer()
{
    QMetaObject::invokeMethod(&m_modifiedTimer, "stop");
}

QUuid Database::publicUuid()
{

    if (!publicCustomData().contains("KPXC_PUBLIC_UUID")) {
        publicCustomData().insert("KPXC_PUBLIC_UUID", QUuid::createUuid().toRfc4122());
        markAsModified();
    }

    return QUuid::fromRfc4122(publicCustomData()["KPXC_PUBLIC_UUID"].toByteArray());
}
