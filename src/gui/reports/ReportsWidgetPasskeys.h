/*
 *  Copyright (C) 2023 KeePassXC Team <team@keepassxc.org>
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

#ifndef KEEPASSXC_REPORTSWIDGETPASSKEYS_H
#define KEEPASSXC_REPORTSWIDGETPASSKEYS_H

#include "gui/entry/EntryModel.h"
#include <QWidget>

class Database;
class Entry;
class Group;
class PasswordHealth;
class QSortFilterProxyModel;
class QStandardItemModel;

namespace Ui
{
    class ReportsWidgetPasskeys;
}

class ReportsWidgetPasskeys : public QWidget
{
    Q_OBJECT
public:
    explicit ReportsWidgetPasskeys(QWidget* parent = nullptr);
    ~ReportsWidgetPasskeys() override;

    void loadSettings(QSharedPointer<Database> db);
    void saveSettings();

protected:
    void showEvent(QShowEvent* event) override;

signals:
    void entryActivated(Entry*);

public slots:
    void updateEntries();
    void emitEntryActivated(const QModelIndex& index);
    void customMenuRequested(QPoint);
    void deleteSelectedEntries();

private slots:
    void selectionChanged();
    void importPasskey();
    void exportPasskey();

private:
    void addPasskeyRow(Group*, Entry*);
    QList<Entry*> getSelectedEntries();

    QScopedPointer<Ui::ReportsWidgetPasskeys> m_ui;

    bool m_entriesUpdated = false;
    QScopedPointer<QStandardItemModel> m_referencesModel;
    QScopedPointer<QSortFilterProxyModel> m_modelProxy;
    QSharedPointer<Database> m_db;
    QList<QPair<Group*, Entry*>> m_rowToEntry;
};

#endif // KEEPASSXC_REPORTSWIDGETPASSKEYS_H
