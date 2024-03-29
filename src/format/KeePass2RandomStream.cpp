/*
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

#include "KeePass2RandomStream.h"

#include "crypto/CryptoHash.h"
#include "format/KeePass2.h"

bool KeePass2RandomStream::init(SymmetricCipher::Mode mode, const QByteArray& key)
{
    switch (mode) {
    case SymmetricCipher::Salsa20: {
        return m_cipher.init(mode,
                             SymmetricCipher::Encrypt,
                             CryptoHash::hash(key, CryptoHash::Sha256),
                             KeePass2::INNER_STREAM_SALSA20_IV);
    }
    case SymmetricCipher::ChaCha20: {
        QByteArray keyIv = CryptoHash::hash(key, CryptoHash::Sha512);
        return m_cipher.init(mode, SymmetricCipher::Encrypt, keyIv.left(32), keyIv.mid(32, 12));
    }
    default:
        qWarning("Invalid stream cipher mode (%d)", mode);
        break;
    }
    return false;
}

QByteArray KeePass2RandomStream::randomBytes(int size, bool* ok)
{
    QByteArray result;

    int bytesRemaining = size;

    while (bytesRemaining > 0) {
        if (m_buffer.size() == m_offset) {
            if (!loadBlock()) {
                *ok = false;
                return {};
            }
        }

        int bytesToCopy = qMin(bytesRemaining, m_buffer.size() - m_offset);
        result.append(m_buffer.mid(m_offset, bytesToCopy));
        m_offset += bytesToCopy;
        bytesRemaining -= bytesToCopy;
    }

    *ok = true;
    return result;
}

QByteArray KeePass2RandomStream::process(const QByteArray& data, bool* ok)
{
    bool randomBytesOk;

    QByteArray randomData = randomBytes(data.size(), &randomBytesOk);
    if (!randomBytesOk) {
        *ok = false;
        return {};
    }

    QByteArray result;
    result.resize(data.size());

    for (int i = 0; i < data.size(); i++) {
        result[i] = data[i] ^ randomData[i];
    }

    *ok = true;
    return result;
}

bool KeePass2RandomStream::processInPlace(QByteArray& data)
{
    bool ok;
    QByteArray randomData = randomBytes(data.size(), &ok);
    if (!ok) {
        return false;
    }

    for (int i = 0; i < data.size(); i++) {
        data[i] = data[i] ^ randomData[i];
    }

    return true;
}

QString KeePass2RandomStream::errorString() const
{
    return m_cipher.errorString();
}

bool KeePass2RandomStream::loadBlock()
{
    Q_ASSERT(m_offset == m_buffer.size());

    m_buffer.fill('\0', m_cipher.blockSize(m_cipher.mode()));
    if (!m_cipher.process(m_buffer)) {
        return false;
    }
    m_offset = 0;

    return true;
}
