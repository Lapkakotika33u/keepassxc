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

#include "OPUXReader.h"

#include "core/Database.h"
#include "core/Entry.h"
#include "core/Group.h"
#include "totp/totp.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedPointer>
#include <QUrl>

#include <minizip/unzip.h>

/*
{
  "accounts": [
    {
      "attrs": {
        "accountName": "Wendy Appleseed",
        "name": "Wendy Appleseed",
        "avatar": "profile-pic.png",
        "email": "wendy.c.appleseed@gmail.com",
        "uuid": "D4RI47B7BJDT25C2LWA7LEJLHZ",
        "domain": "https://my.1password.com/"
      },
      "vaults": [
        {
          "attrs": {
            "uuid": "rr3lr6c2opoggvrete23q72ahi",
            "desc": "",
            "avatar": "pic.png",
            "name": "Personal",
            "type": "P"
          },
          "items": [
            {
              "item": {
                "uuid": "fkruyzrldvizuqlnavfj3gltfe",
                "favIndex": 1,
                "createdAt": 1614298956,
                "updatedAt": 1635346445,
                "trashed": false,
                "categoryUuid": "001",
                "details": {
                  "loginFields": [
                    {
                      "value": "most-secure-password-ever!",
                      "id": "",
                      "name": "password",
                      "fieldType": "P",
                      "designation": "password"
                    }
                  ],
                  "notesPlain": "This is a note. *bold*! _italic_!",
                  "sections": [
                    {
                      "title": "Security",
                      "name": "Section_oazxddhvftfknycbbmh5ntwfa4",
                      "fields": [
                        {
                          "title": "PIN",
                          "id": "CCEF647B399604E8F6Q6C8C3W31AFD407",
                          "value": {
                            "concealed": "12345"
                          },
                          "indexAtSource": 0,
                          "guarded": false,
                          "multiline": false,
                          "dontGenerate": false,
                          "inputTraits": {
                            "keyboard": "default",
                            "correction": "default",
                            "capitalization": "default"
                          }
                        } // fieldObject
                      ] // "fields"
                    } // sectionObject
                  ], // "sections"
                  "passwordHistory": [
                    {
                      "value": "12345password",
                      "time": 1458322355
                    }
                  ],
                  "documentAttributes": {
                    "fileName": "My movie.mp4",
                    "documentId": "o2xjvw2q5j2yx6rtpxfjdqopom",
                    "decryptedSize": 3605932
                  }
                }, // "details"
                "overview": {
                  "subtitle": "",
                  "urls": [
                    {
                      "label": "",
                      "url": "https://www.dropbox.com/"
                    }
                  ],
                  "title": "Dropbox",
                  "url": "https://www.dropbox.com/",
                  "ps": 100,
                  "pbe": 86.13621,
                  "pgrng": true
                } // "overview"
              } // "item"
            } // itemObject
          ] // items
        } // vaultObject
      ] // vaults
    } // accountObject
  ] // accounts
}
*/

namespace
{
    Entry* readItem(const QJsonObject& item)
    {
        auto itemMap = item.toVariantMap();
        auto overviewMap = itemMap.value("overview").toMap();
        auto detailsMap = itemMap.value("details").toMap();

        // Create entry and assign basic values
        QScopedPointer<Entry> entry(new Entry());
        entry->setUuid(QUuid::createUuid());
        entry->setTitle(overviewMap.value("title").toString());
        entry->setUrl(overviewMap.value("url").toString());
        if (overviewMap.contains("urls")) {
            int i = 1;
            for (auto urlRaw : overviewMap.value("urls").toList()) {
                auto urlMap = urlRaw.toMap();
                auto url = urlMap.value("url").toString();
                if (entry->url() != url) {
                    entry->attributes()->set(QString("KP2A_URL_%1").arg(i), url);
                    ++i;
                }
            }
        }
        if (overviewMap.contains("tags")) {
            entry->setTags(overviewMap.value("tags").toStringList().join(","));
        }
        if (itemMap.value("favIndex").toString() == "1") {
            entry->addTag(QObject::tr("Favorite", "Tag for favorite entries"));
        }
        if (itemMap.value("state").toString() == "archived") {
            entry->addTag(QObject::tr("Archived", "Tag for archived entries"));
        }

        // Parse the details map by setting the username, password, and notes first
        auto loginFields = detailsMap.value("loginFields").toList();
        for (const auto& field : loginFields) {
            auto fieldMap = field.toMap();
            auto designation = fieldMap.value("designation").toString();
            if (designation.compare("username", Qt::CaseInsensitive) == 0) {
                entry->setUsername(fieldMap.value("value").toString());
            } else if (designation.compare("password", Qt::CaseInsensitive) == 0) {
                entry->setPassword(fieldMap.value("value").toString());
            }
        }
        entry->setNotes(detailsMap.value("notesPlain").toString());

        // Dive into the item sections to pull out advanced attributes
        auto sections = detailsMap.value("sections").toList();
        for (const auto& section : sections) {
            auto sectionMap = section.toMap();
            auto prefix = sectionMap.value("title").toString();
            if (prefix.isEmpty()) {
                prefix = QUuid::createUuid().toString().mid(1, 5);
            }

            for (const auto& field : sectionMap.value("fields").toList()) {
                auto fieldMap = field.toMap();
                auto name = fieldMap.value("title").toString();
                if (name.isEmpty()) {
                    name = fieldMap.value("id").toString();
                }
                name = QString("%1_%2").arg(prefix, name);

                auto valueMap = fieldMap.value("value").toMap();
                auto key = valueMap.firstKey();
                if (key == "totp") {
                    // Build otpauth url
                    QUrl otpurl(QString("otpauth://totp/%1:%2?secret=%3")
                                    .arg(entry->title(), entry->username(), valueMap.value(key).toString()));

                    if (entry->hasTotp()) {
                        // Store multiple TOTP definitions as additional otp attributes
                        int i = 0;
                        name = "otp";
                        auto attributes = entry->attributes()->keys();
                        while (attributes.contains(name)) {
                            name = QString("otp_%1").arg(++i);
                        }
                        entry->attributes()->set(name, otpurl.toEncoded(), true);
                    } else {
                        entry->setTotp(Totp::parseSettings(otpurl.toEncoded()));
                    }
                } else {
                    QString value = valueMap.value(key).toString();
                    if (key == "date") {
                        value = QDateTime::fromSecsSinceEpoch(valueMap.value(key).toULongLong(), Qt::UTC).toString();
                    } else if (key == "email") {
                        value = valueMap.value(key).toMap().value("email_address").toString();
                    } else if (key == "address") {
                        auto address = valueMap.value(key).toMap();
                        value = address.value("street").toString() + "\n" + address.value("city").toString() + ", "
                                + address.value("state").toString() + " " + address.value("zip").toString() + "\n"
                                + address.value("country").toString();
                    }

                    if (!value.isEmpty()) {
                        entry->attributes()->set(name, value, key == "concealed");
                    }
                }
            }
        }

        // TODO: add attachments
        // TODO: add icon?

        // Collapse any accumulated history
        entry->removeHistoryItems(entry->historyItems());

        // Adjust the created and modified times
        auto timeInfo = entry->timeInfo();
        auto createdTime = QDateTime::fromSecsSinceEpoch(itemMap.value("createdAt").toULongLong(), Qt::UTC);
        auto modifiedTime = QDateTime::fromSecsSinceEpoch(itemMap.value("updatedAt").toULongLong(), Qt::UTC);
        timeInfo.setCreationTime(createdTime);
        timeInfo.setLastModificationTime(modifiedTime);
        timeInfo.setLastAccessTime(modifiedTime);
        entry->setTimeInfo(timeInfo);

        return entry.take();
    }

    Group* readVault(const QJsonObject& vault)
    {
        if (!vault.contains("attrs") || !vault.contains("items")) {
            return nullptr;
        }

        // Create group and assign basic values
        QScopedPointer<Group> group(new Group());
        group->setUuid(QUuid::createUuid());
        group->setName(vault.value("attrs").toObject().value("name").toString());

        const auto items = vault.value("items").toArray();
        for (const auto& item : items) {
            auto entry = readItem(item.toObject());
            if (entry) {
                entry->setGroup(group.data(), false);
            }
        }
        
        // TODO: add icon

        return group.take();
    }
} // namespace

bool OPUXReader::hasError()
{
    return !m_error.isEmpty();
}

QString OPUXReader::errorString()
{
    return m_error;
}

QSharedPointer<Database> OPUXReader::convert(const QString& path)
{
    m_error.clear();

    QFileInfo fileinfo(path);
    if (!fileinfo.exists()) {
        m_error = QObject::tr("File does not exist.").arg(path);
        return {};
    }

    // 1PUX is a zip file format, open it and process the contents in memory
    auto uf = unzOpen64(fileinfo.absoluteFilePath().toLatin1().constData());
    if (!uf) {
        m_error = QObject::tr("Invalid 1PUX file format: Not a valid ZIP file.");
        return {};
    }

    // Find the export.data file, if not found this isn't a 1PUX file
    if (unzLocateFile(uf, "export.data", 2) != UNZ_OK) {
        m_error = QObject::tr("Invalid 1PUX file format: Missing export.data");
        unzClose(uf);
        return {};
    }

    // Read export.data into memory
    QByteArray data;
    int bytes, bytesRead = 0;
    unzOpenCurrentFile(uf);
    do {
        data.resize(data.size() + 8192);
        bytes = unzReadCurrentFile(uf, data.data() + bytesRead, 8192);
        if (bytes > 0) {
            bytesRead += bytes;
        }
    } while (bytes > 0);
    unzCloseCurrentFile(uf);
    data.truncate(bytesRead);

    auto db = QSharedPointer<Database>::create();
    const auto json = QJsonDocument::fromJson(data);

    const auto account = json.object().value("accounts").toArray().first().toObject();
    const auto vaults = account.value("vaults").toArray();

    for (const auto& vault : vaults) {
        auto group = readVault(vault.toObject());
        if (group) {
            group->setParent(db->rootGroup());
        }
    }

    unzClose(uf);
    return db;
}
