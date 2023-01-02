/*
 *  Copyright (C) 2019 KeePassXC Team <team@keepassxc.org>
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

#include "ImportOpenWidget.h"
#include "ui_DatabaseOpenWidget.h"

#include "core/Database.h"
#include "format/OpVaultReader.h"
#include "format/OPUXReader.h"
#include "format/KeePass1Reader.h"

#include <QDir>

ImportOpenWidget::ImportOpenWidget(QWidget* parent)
    : DatabaseOpenWidget(parent)
{
    
}

void ImportOpenWidget::setImportType(ImportType type)
{
    m_importType = type;

    switch (m_importType) {
    case ImportType::IMPORT_OPVAULT:
        m_ui->labelHeadline->setText("Import 1Password Database");
        break;
    case ImportType::IMPORT_OPUX:
        m_ui->labelHeadline->setText("Import 1Password 1PUX File");
        break;
    case ImportType::IMPORT_KEEPASS1:
        m_ui->labelHeadline->setText("Import KeePass1 Database");
        break;
    default:
        // Should never get here
        Q_ASSERT(false);
        break;
    }
}

void ImportOpenWidget::openDatabase()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    switch (m_importType) {
    case ImportType::IMPORT_OPVAULT:
        m_db = import1Password();
        break;
    case ImportType::IMPORT_OPUX:
        m_db = importOPUX();
        break;
    case ImportType::IMPORT_KEEPASS1:
        m_db = importKeePass1();
        break;
    default:
        m_db.reset();
        m_error = tr("Import widget is unintialized!");
        break;
    }

    QApplication::restoreOverrideCursor();

    if (m_db) {
        emit dialogFinished(true);
    } else {
        m_ui->messageWidget->showMessage(tr("There was a problem importing the database:\n%1").arg(m_error),
                                         MessageWidget::Error);
        clearForms();
    }
}

QSharedPointer<Database> ImportOpenWidget::import1Password()
{
    m_error.clear();

    OpVaultReader reader;
    QDir opVault(m_filename);
    auto db = reader.readDatabase(opVault, m_ui->editPassword->text());
    if (reader.hasError()) {
        m_error = reader.errorString();
    }
    return QSharedPointer<Database>(db);
}

QSharedPointer<Database> ImportOpenWidget::importOPUX()
{
    m_error.clear();

    OPUXReader reader;
    auto db = reader.convert(m_filename);
    if (reader.hasError()) {
        m_error = reader.errorString();
    }
    return db;
}

QSharedPointer<Database> ImportOpenWidget::importKeePass1()
{
    KeePass1Reader reader;

    QString password;
    QString keyFileName = m_ui->keyFileLineEdit->text();

    if (!m_ui->editPassword->text().isEmpty() || m_retryUnlockWithEmptyPassword) {
        password = m_ui->editPassword->text();
    }

    auto db = reader.readDatabase(m_filename, password, keyFileName);
    if (reader.hasError()) {
        m_error = reader.errorString();
    }

    return db;
}
