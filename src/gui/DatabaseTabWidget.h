/*
 * Copyright (C) 2023 KeePassXC Team <team@keepassxc.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 or (at your option)
 * version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KEEPASSX_DATABASETABWIDGET_H
#define KEEPASSX_DATABASETABWIDGET_H

#include "DatabaseOpenDialog.h"
#include "config-keepassx.h"
#include "gui/MessageWidget.h"

#include <QTabWidget>
#include <QTimer>

class Database;
class DatabaseWidget;
class DatabaseWidgetStateSync;
class DatabaseOpenWidget;

class DatabaseTabWidget : public QTabWidget
{
    Q_OBJECT

public:
    explicit DatabaseTabWidget(QWidget* parent = nullptr);
    ~DatabaseTabWidget() override;
    void mergeDatabase(const QString& filePath);

    QString tabName(int index);
    DatabaseWidget* currentDatabaseWidget();
    DatabaseWidget* databaseWidgetFromIndex(int index) const;

    bool canSave(int index = -1) const;
    bool isModified(int index = -1) const;
    bool hasLockableDatabases() const;

public slots:
    void lockAndSwitchToFirstUnlockedDatabase(int index = -1);
    void addDatabaseTab(const QString& filePath,
                        bool inBackground = false,
                        const QString& password = {},
                        const QString& keyfile = {});
    void addDatabaseTab(DatabaseWidget* dbWidget, bool inBackground = false);
    bool closeDatabaseTab(int index);
    bool closeDatabaseTab(DatabaseWidget* dbWidget);
    bool closeAllDatabaseTabs();
    bool closeCurrentDatabaseTab();
    bool closeDatabaseTabFromSender();
    void updateTabName(int index = -1);

    DatabaseWidget* newDatabase();
    void openDatabase();
    void mergeDatabase();
    void importCsv();
    void importKeePass1Database();
    void importOpVaultDatabase();
    bool saveDatabase(int index = -1);
    bool saveDatabaseAs(int index = -1);
    bool saveDatabaseBackup(int index = -1);
    void exportToCsv();
    void exportToHtml();
    void exportToXML();

    bool lockDatabases();
    void lockDatabasesDelayed();
    void closeDatabaseFromSender();
    void unlockDatabaseInDialog(DatabaseWidget* dbWidget, DatabaseOpenDialog::Intent intent);
    void unlockDatabaseInDialog(DatabaseWidget* dbWidget, DatabaseOpenDialog::Intent intent, const QString& filePath);
    void unlockAnyDatabaseInDialog(DatabaseOpenDialog::Intent intent);
    void relockPendingDatabase();

    void showDatabaseSecurity();
    void showDatabaseReports();
    void showDatabaseSettings();
#ifdef WITH_XC_BROWSER_PASSKEYS
    void showPasskeys();
    void importPasskey();
    void importPasskeyToEntry();
#endif
    void performGlobalAutoType(const QString& search);
    void performBrowserUnlock();

signals:
    void databaseOpened(DatabaseWidget* dbWidget);
    void databaseClosed(const QString& filePath);
    void databaseUnlocked(DatabaseWidget* dbWidget);
    void databaseLocked(DatabaseWidget* dbWidget);
    void activeDatabaseChanged(DatabaseWidget* dbWidget);
    void tabNameChanged();
    void tabVisibilityChanged(bool tabsVisible);
    void messageGlobal(const QString&, MessageWidget::MessageType type);
    void messageDismissGlobal();
    void databaseUnlockDialogFinished(bool accepted, DatabaseWidget* dbWidget);

private slots:
    void toggleTabbar();
    void emitActiveDatabaseChanged();
    void emitDatabaseLockChanged();
    void handleDatabaseUnlockDialogFinished(bool accepted, DatabaseWidget* dbWidget);
    void handleExportError(const QString& reason);
    void updateLastDatabases();

private:
    QSharedPointer<Database> execNewDatabaseWizard();
    void updateLastDatabases(const QString& filename);
    bool warnOnExport();
    void displayUnlockDialog();

    QPointer<DatabaseWidgetStateSync> m_dbWidgetStateSync;
    QPointer<DatabaseWidget> m_dbWidgetPendingLock;
    QPointer<DatabaseOpenDialog> m_databaseOpenDialog;
    QTimer m_lockDelayTimer;
    bool m_databaseOpenInProgress;
};

#endif // KEEPASSX_DATABASETABWIDGET_H
