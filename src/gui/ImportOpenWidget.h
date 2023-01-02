/*
 *  Copyright (C) 2022 KeePassXC Team <team@keepassxc.org>
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

#ifndef KEEPASSXC_IMPORTOPENWIDGET_H
#define KEEPASSXC_IMPORTOPENWIDGET_H

#include "gui/DatabaseOpenWidget.h"

class ImportOpenWidget : public DatabaseOpenWidget
{
    Q_OBJECT

public:
    enum class ImportType
    {
        IMPORT_NONE = 0,
        IMPORT_OPVAULT,
        IMPORT_OPUX,
        IMPORT_KEEPASS1
    };

    explicit ImportOpenWidget(QWidget* parent = nullptr);
    void setImportType(ImportType type);

protected:
    void openDatabase() override;

private:
    QSharedPointer<Database> import1Password();
    QSharedPointer<Database> importOPUX();
    QSharedPointer<Database> importKeePass1();

    ImportType m_importType = ImportType::IMPORT_NONE;
    QString m_error;
};

#endif // KEEPASSXC_IMPORTOPENWIDGET_H
