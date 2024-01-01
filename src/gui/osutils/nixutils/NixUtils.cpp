/*
 * Copyright (C) 2020 KeePassXC Team <team@keepassxc.org>
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

#include "NixUtils.h"

#include "config-keepassx.h"

#include <QApplication>
#include <QDBusInterface>
#include <QDebug>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QStyle>
#include <QTextStream>
#ifdef WITH_XC_X11
#include <QX11Info>

#include <qpa/qplatformnativeinterface.h>

#include "X11Funcs.h"
#include <X11/XKBlib.h>
#include <xcb/xproto.h>

namespace
{
    Display* dpy;
    Window rootWindow;
    bool x11ErrorOccurred = false;

    int x11ErrorHandler(Display*, XErrorEvent*)
    {
        x11ErrorOccurred = true;
        return 1;
    }
} // namespace
#endif

QPointer<NixUtils> NixUtils::m_instance = nullptr;

NixUtils* NixUtils::instance()
{
    if (!m_instance) {
        m_instance = new NixUtils(qApp);
    }

    return m_instance;
}

NixUtils::NixUtils(QObject* parent)
    : OSUtilsBase(parent)
{
#ifdef WITH_XC_X11
    dpy = QX11Info::display();
    rootWindow = QX11Info::appRootWindow();
#endif

    // notify about system color scheme changes
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    sessionBus.connect("org.freedesktop.portal.Desktop",
                       "/org/freedesktop/portal/desktop",
                       "org.freedesktop.portal.Settings",
                       "SettingChanged",
                       this,
                       SLOT(handleColorSchemeChanged(QString, QString, QDBusVariant)));

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", "Read");
    msg << QVariant("org.freedesktop.appearance") << QVariant("color-scheme");
    sessionBus.callWithCallback(msg, this, SLOT(handleColorSchemeRead(QDBusVariant)));
}

NixUtils::~NixUtils() = default;

bool NixUtils::isDarkMode() const
{
    // prefer freedesktop "org.freedesktop.appearance color-scheme" setting
    if (m_systemColorschemePrefExists) {
        return m_systemColorschemePref == ColorschemePref::PreferDark;
    }

    if (!qApp || !qApp->style()) {
        return false;
    }
    return qApp->style()->standardPalette().color(QPalette::Window).toHsl().lightness() < 110;
}

bool NixUtils::isStatusBarDark() const
{
    // TODO: implement
    return isDarkMode();
}

QString NixUtils::getAutostartDesktopFilename(bool createDirs) const
{
    QDir autostartDir;
    auto confHome = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (confHome.isEmpty()) {
        return {};
    }
    autostartDir.setPath(confHome + QStringLiteral("/autostart"));
    if (createDirs && !autostartDir.exists()) {
        autostartDir.mkpath(".");
    }

    return QFile(autostartDir.absoluteFilePath(qApp->property("KPXC_QUALIFIED_APPNAME").toString().append(".desktop")))
        .fileName();
}

bool NixUtils::isLaunchAtStartupEnabled() const
{
    return QFile::exists(getAutostartDesktopFilename());
    ;
}

void NixUtils::setLaunchAtStartup(bool enable)
{
    if (enable) {
        QFile desktopFile(getAutostartDesktopFilename(true));
        if (!desktopFile.open(QIODevice::WriteOnly)) {
            qWarning("Failed to create autostart desktop file.");
            return;
        }

        const QString appImagePath = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
        const bool isAppImage = !appImagePath.isNull() && QFile::exists(appImagePath);
        const QString executeablePathOrName = isAppImage ? appImagePath : QApplication::applicationName().toLower();

        QTextStream stream(&desktopFile);
        stream.setCodec("UTF-8");
        stream << QStringLiteral("[Desktop Entry]") << '\n'
               << QStringLiteral("Name=") << QApplication::applicationDisplayName() << '\n'
               << QStringLiteral("GenericName=") << tr("Password Manager") << '\n'
               << QStringLiteral("Exec=") << executeablePathOrName << '\n'
               << QStringLiteral("TryExec=") << executeablePathOrName << '\n'
               << QStringLiteral("Icon=") << QApplication::applicationName().toLower() << '\n'
               << QStringLiteral("StartupWMClass=keepassxc") << '\n'
               << QStringLiteral("StartupNotify=true") << '\n'
               << QStringLiteral("Terminal=false") << '\n'
               << QStringLiteral("Type=Application") << '\n'
               << QStringLiteral("Version=1.0") << '\n'
               << QStringLiteral("Categories=Utility;Security;Qt;") << '\n'
               << QStringLiteral("MimeType=application/x-keepass2;") << '\n'
               << QStringLiteral("X-GNOME-Autostart-enabled=true") << '\n'
               << QStringLiteral("X-GNOME-Autostart-Delay=2") << '\n'
               << QStringLiteral("X-KDE-autostart-after=panel") << '\n'
               << QStringLiteral("X-LXQt-Need-Tray=true") << endl;
        desktopFile.close();
    } else if (isLaunchAtStartupEnabled()) {
        QFile::remove(getAutostartDesktopFilename());
    }
}

bool NixUtils::isCapslockEnabled()
{
#ifdef WITH_XC_X11
    QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();
    auto* display = native->nativeResourceForWindow("display", nullptr);
    if (!display) {
        return false;
    }

    QString platform = QGuiApplication::platformName();
    if (platform == "xcb") {
        unsigned state = 0;
        if (XkbGetIndicatorState(reinterpret_cast<Display*>(display), XkbUseCoreKbd, &state) == Success) {
            return ((state & 1u) != 0);
        }
    }
#endif

    // TODO: Wayland

    return false;
}

void NixUtils::registerNativeEventFilter()
{
    qApp->installNativeEventFilter(this);
}

bool NixUtils::nativeEventFilter(const QByteArray& eventType, void* message, long*)
{
#ifdef WITH_XC_X11
    if (eventType != QByteArrayLiteral("xcb_generic_event_t")) {
        return false;
    }

    auto* genericEvent = static_cast<xcb_generic_event_t*>(message);
    quint8 type = genericEvent->response_type & 0x7f;

    if (type == XCB_KEY_PRESS) {
        auto* keyPressEvent = static_cast<xcb_key_press_event_t*>(message);
        auto modifierMask = ControlMask | ShiftMask | Mod1Mask | Mod4Mask;
        return triggerGlobalShortcut(keyPressEvent->detail, keyPressEvent->state & modifierMask);
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif
    return false;
}

bool NixUtils::triggerGlobalShortcut(uint keycode, uint modifiers)
{
#ifdef WITH_XC_X11
    QHashIterator<QString, QSharedPointer<globalShortcut>> i(m_globalShortcuts);
    while (i.hasNext()) {
        i.next();
        if (i.value()->nativeKeyCode == keycode && i.value()->nativeModifiers == modifiers) {
            emit globalShortcutTriggered(i.key());
            return true;
        }
    }
#else
    Q_UNUSED(keycode)
    Q_UNUSED(modifiers)
#endif
    return false;
}

bool NixUtils::registerGlobalShortcut(const QString& name, Qt::Key key, Qt::KeyboardModifiers modifiers, QString* error)
{
#ifdef WITH_XC_X11
    auto keycode = XKeysymToKeycode(dpy, qcharToNativeKeyCode(key));
    auto modifierscode = qtToNativeModifiers(modifiers);

    // Check if this key combo is registered to another shortcut
    QHashIterator<QString, QSharedPointer<globalShortcut>> i(m_globalShortcuts);
    while (i.hasNext()) {
        i.next();
        if (i.value()->nativeKeyCode == keycode && i.value()->nativeModifiers == modifierscode && i.key() != name) {
            if (error) {
                *error = tr("Global shortcut already registered to %1").arg(i.key());
            }
            return false;
        }
    }

    unregisterGlobalShortcut(name);

    x11ErrorOccurred = false;
    auto prevHandler = XSetErrorHandler(x11ErrorHandler);

    XGrabKey(dpy, keycode, modifierscode, rootWindow, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, keycode, modifierscode | Mod2Mask, rootWindow, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, keycode, modifierscode | LockMask, rootWindow, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, keycode, modifierscode | Mod2Mask | LockMask, rootWindow, True, GrabModeAsync, GrabModeAsync);

    XSync(dpy, False);
    XSetErrorHandler(prevHandler);

    if (x11ErrorOccurred) {
        x11ErrorOccurred = false;
        if (error) {
            *error = tr("Could not register global shortcut");
        }
        return false;
    }

    auto gs = QSharedPointer<globalShortcut>::create();
    gs->nativeKeyCode = keycode;
    gs->nativeModifiers = modifierscode;
    m_globalShortcuts.insert(name, gs);
#else
    Q_UNUSED(name)
    Q_UNUSED(key)
    Q_UNUSED(modifiers)
    Q_UNUSED(error)
#endif
    return true;
}

bool NixUtils::unregisterGlobalShortcut(const QString& name)
{
#ifdef WITH_XC_X11
    if (!m_globalShortcuts.contains(name)) {
        return false;
    }

    auto gs = m_globalShortcuts.value(name);
    XUngrabKey(dpy, gs->nativeKeyCode, gs->nativeModifiers, rootWindow);
    XUngrabKey(dpy, gs->nativeKeyCode, gs->nativeModifiers | Mod2Mask, rootWindow);
    XUngrabKey(dpy, gs->nativeKeyCode, gs->nativeModifiers | LockMask, rootWindow);
    XUngrabKey(dpy, gs->nativeKeyCode, gs->nativeModifiers | Mod2Mask | LockMask, rootWindow);

    m_globalShortcuts.remove(name);
#else
    Q_UNUSED(name)
#endif
    return true;
}

void NixUtils::handleColorSchemeRead(QDBusVariant value)
{
    value = qvariant_cast<QDBusVariant>(value.variant());
    setColorScheme(value);
}

void NixUtils::handleColorSchemeChanged(QString ns, QString key, QDBusVariant value)
{
    if (ns == "org.freedesktop.appearance" && key == "color-scheme") {
        setColorScheme(value);
    }
}

void NixUtils::setColorScheme(QDBusVariant value)
{
    m_systemColorschemePref = static_cast<ColorschemePref>(value.variant().toInt());
    m_systemColorschemePrefExists = true;
    emit interfaceThemeChanged();
}

quint64 NixUtils::getProcessStartTime() const
{
    QString processStatPath = QString("/proc/%1/stat").arg(QCoreApplication::applicationPid());
    QFile processStatFile(processStatPath);

    if (!processStatFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "nixutils: failed to open " << processStatPath;
        return 0;
    }

    QTextStream processStatStream(&processStatFile);
    QString processStatInfo = processStatStream.readLine();
    processStatFile.close();

    auto startIndex = processStatInfo.indexOf(')', -1);
    if (startIndex != -1) {
        auto tokens = processStatInfo.midRef(startIndex + 2).split(' ');
        if (tokens.size() >= 20) {
            return tokens[19].toULongLong();
        }
    }

    qDebug() << "nixutils: failed to parse " << processStatPath;
    return 0;
}
