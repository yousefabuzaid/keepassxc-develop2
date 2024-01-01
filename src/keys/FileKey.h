/*
 *  Copyright (C) 2017 KeePassXC Team <team@keepassxc.org>
 *  Copyright (C) 2011 Felix Geyer <debfx@fobos.de>
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

#ifndef KEEPASSX_FILEKEY_H
#define KEEPASSX_FILEKEY_H

#include <botan/mem_ops.h>
#include <botan/secmem.h>

#include "keys/Key.h"

class QIODevice;

class FileKey : public Key
{
public:
    static QUuid UUID;

    enum Type
    {
        None,
        Hashed,
        KeePass2XML,
        KeePass2XMLv2,
        FixedBinary,
        FixedBinaryHex
    };

    FileKey();
    ~FileKey() override = default;
    bool load(QIODevice* device, QString* errorMsg = nullptr);
    bool load(const QString& fileName, QString* errorMsg = nullptr);
    QByteArray rawKey() const override;
    void setRawKey(const QByteArray& data) override;
    Type type() const;
    static void createRandom(QIODevice* device, int size = 128);
    static void createXMLv2(QIODevice* device, int size = 32);
    static bool create(const QString& fileName, QString* errorMsg = nullptr);

    QByteArray serialize() const override;
    void deserialize(const QByteArray& data) override;

private:
    static constexpr int SHA256_SIZE = 32;

    bool loadXml(QIODevice* device, QString* errorMsg = nullptr);
    bool loadBinary(QIODevice* device);
    bool loadHex(QIODevice* device);
    bool loadHashed(QIODevice* device);

    Botan::secure_vector<char> m_key;
    Type m_type = None;
    QString m_file;
};

#endif // KEEPASSX_FILEKEY_H
