/*
 *  Copyright (C) 2018 KeePassXC Team <team@keepassxc.org>
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

#ifndef KEEPASSXC_NEWDATABASEWIZARD_H
#define KEEPASSXC_NEWDATABASEWIZARD_H

#include <QPointer>
#include <QWizard>

class Database;
class NewDatabaseWizardPage;

/**
 * Setup wizard for creating a new database.
 */
class NewDatabaseWizard : public QWizard
{
    Q_OBJECT

public:
    explicit NewDatabaseWizard(QWidget* parent = nullptr);
    ~NewDatabaseWizard() override;

    QSharedPointer<Database> takeDatabase();
    bool validateCurrentPage() override;

protected:
    void initializePage(int id) override;

private:
    QSharedPointer<Database> m_db;
    QList<QPointer<NewDatabaseWizardPage>> m_pages;
};

#endif // KEEPASSXC_NEWDATABASEWIZARD_H
