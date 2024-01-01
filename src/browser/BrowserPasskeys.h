/*
 *  Copyright (C) 2023 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BROWSERPASSKEYS_H
#define BROWSERPASSKEYS_H

#include "BrowserCbor.h"
#include <QJsonObject>
#include <QObject>

#include <botan/asn1_obj.h>
#include <botan/bigint.h>

#define ID_BYTES 32
#define HASH_BYTES 32
#define DEFAULT_TIMEOUT 300000
#define DEFAULT_DISCOURAGED_TIMEOUT 120000
#define RSA_BITS 2048
#define RSA_EXPONENT 65537

enum AuthDataOffsets : int
{
    RPIDHASH = 0,
    FLAGS = 32,
    SIGNATURE_COUNTER = 33,
    AAGUID = 37,
    CREDENTIAL_LENGTH = 53,
    CREDENTIAL_ID = 55
};

enum AuthenticatorFlags
{
    UP = 0,
    UV = 2,
    BE = 3,
    BS = 4,
    AT = 6,
    ED = 7
};

struct PublicKeyCredential
{
    QString credentialId;
    QJsonObject response;
    QByteArray key;
};

struct PrivateKey
{
    QByteArray cborEncoded;
    QByteArray pem;
};

// Predefined variables used for testing the class
struct TestingVariables
{
    QString credentialId;
    QString first;
    QString second;
};

class BrowserPasskeys : public QObject
{
    Q_OBJECT

public:
    explicit BrowserPasskeys() = default;
    ~BrowserPasskeys() = default;
    static BrowserPasskeys* instance();

    PublicKeyCredential buildRegisterPublicKeyCredential(const QJsonObject& publicKeyCredentialOptions,
                                                         const QString& origin,
                                                         const TestingVariables& predefinedVariables = {});
    QJsonObject buildGetPublicKeyCredential(const QJsonObject& publicKeyCredentialRequestOptions,
                                            const QString& origin,
                                            const QString& credentialId,
                                            const QString& userHandle,
                                            const QString& privateKeyPem);
    bool isUserVerificationValid(const QString& userVerification) const;
    int getTimeout(const QString& userVerification, int timeout) const;
    QStringList getAllowedCredentialsFromPublicKey(const QJsonObject& publicKey) const;

    static const QString PUBLIC_KEY;
    static const QString REQUIREMENT_DISCOURAGED;
    static const QString REQUIREMENT_PREFERRED;
    static const QString REQUIREMENT_REQUIRED;

    static const QString PASSKEYS_ATTESTATION_DIRECT;
    static const QString PASSKEYS_ATTESTATION_NONE;

    static const QString KPEX_PASSKEY_USERNAME;
    static const QString KPEX_PASSKEY_GENERATED_USER_ID;
    static const QString KPEX_PASSKEY_PRIVATE_KEY_PEM;
    static const QString KPEX_PASSKEY_RELYING_PARTY;
    static const QString KPEX_PASSKEY_USER_HANDLE;

private:
    QJsonObject buildClientDataJson(const QJsonObject& publicKey, const QString& origin, bool get);
    PrivateKey buildAttestationObject(const QJsonObject& publicKey,
                                      const QString& extensions,
                                      const QString& credentialId,
                                      const TestingVariables& predefinedVariables = {});
    QByteArray buildGetAttestationObject(const QJsonObject& publicKey);
    PrivateKey buildCredentialPrivateKey(int alg,
                                         const QString& predefinedFirst = QString(),
                                         const QString& predefinedSecond = QString());
    QByteArray
    buildSignature(const QByteArray& authenticatorData, const QByteArray& clientData, const QString& privateKeyPem);
    QByteArray buildExtensionData(QJsonObject& extensionObject) const;
    QJsonObject parseAuthData(const QByteArray& authData) const;
    QJsonObject parseFlags(const QByteArray& flags) const;
    char setFlagsFromJson(const QJsonObject& flags) const;
    WebAuthnAlgorithms getAlgorithmFromPublicKey(const QJsonObject& publicKey) const;
    QByteArray bigIntToQByteArray(Botan::BigInt& bigInt) const;

    Q_DISABLE_COPY(BrowserPasskeys);

    friend class TestPasskeys;

private:
    BrowserCbor m_browserCbor;
};

static inline BrowserPasskeys* browserPasskeys()
{
    return BrowserPasskeys::instance();
}

#endif // BROWSERPASSKEYS_H
