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

#include "HtmlExporter.h"

#include <QBuffer>
#include <QFile>

#include "core/Group.h"
#include "core/Metadata.h"
#include "gui/Icons.h"

namespace
{
    QString PixmapToHTML(const QPixmap& pixmap)
    {
        if (pixmap.isNull()) {
            return "";
        }

        // Based on https://stackoverflow.com/a/6621278
        QByteArray a;
        QBuffer buffer(&a);
        pixmap.save(&buffer, "PNG");
        return QString("<img src=\"data:image/png;base64,") + a.toBase64() + "\"/>";
    }

    QString formatEntry(const Entry& entry)
    {
        // Here we collect the table rows with this entry's data fields
        QString item;

        // Output the fixed fields
        const auto& u = entry.username();
        if (!u.isEmpty()) {
            item.append("<tr><th>");
            item.append(QObject::tr("User name"));
            item.append("</th><td class=\"username\">");
            item.append(entry.username().toHtmlEscaped());
            item.append("</td></tr>");
        }

        const auto& p = entry.password();
        if (!p.isEmpty()) {
            item.append("<tr><th>");
            item.append(QObject::tr("Password"));
            item.append("</th><td class=\"password\">");
            item.append(entry.password().toHtmlEscaped());
            item.append("</td></tr>");
        }

        const auto& r = entry.url();
        if (!r.isEmpty()) {
            item.append("<tr><th>");
            item.append(QObject::tr("URL"));
            item.append("</th><td class=\"url\"><a href=\"");
            item.append(r.toHtmlEscaped());
            item.append("\">");

            // Restrict the length of what we display of the URL -
            // even from a paper backup, nobody will every type in
            // more than 100 characters of a URL
            constexpr auto maxlen = 100;
            if (r.size() <= maxlen) {
                item.append(r.toHtmlEscaped());
            } else {
                item.append(r.mid(0, maxlen).toHtmlEscaped());
                item.append("&hellip;");
            }

            item.append("</a></td></tr>");
        }

        // Now add the attributes (if there are any)
        const auto* const attr = entry.attributes();
        if (attr && !attr->customKeys().isEmpty()) {
            for (const auto& key : attr->customKeys()) {
                item.append("<tr><th>");
                item.append(key.toHtmlEscaped());
                item.append("</th><td class=\"attr\">");
                item.append(attr->value(key).toHtmlEscaped().replace(" ", "&nbsp;").replace("\n", "<br>"));
                item.append("</td></tr>");
            }
        }

        const auto& n = entry.notes();
        if (!n.isEmpty()) {
            item.append("<tr><th>");
            item.append(QObject::tr("Notes"));
            item.append("</th><td class=\"notes\">");
            item.append(entry.notes().toHtmlEscaped().replace("\n", "<br>"));
            item.append("</td></tr>");
        }
        return item;
    }
} // namespace

bool HtmlExporter::exportDatabase(const QString& filename,
                                  const QSharedPointer<const Database>& db,
                                  bool sorted,
                                  bool ascending)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = file.errorString();
        return false;
    }
    return exportDatabase(&file, db, sorted, ascending);
}

QString HtmlExporter::errorString() const
{
    return m_error;
}

bool HtmlExporter::exportDatabase(QIODevice* device,
                                  const QSharedPointer<const Database>& db,
                                  bool sorted,
                                  bool ascending)
{
    const auto meta = db->metadata();
    if (!meta) {
        m_error = "Internal error: metadata is NULL";
        return false;
    }

    const auto header = QString("<html>"
                                "<head>"
                                "<meta charset=\"UTF-8\">"
                                "<title>"
                                + meta->name().toHtmlEscaped()
                                + "</title>"
                                  "<style>"
                                  "body "
                                  "{ font-family: \"Open Sans\", Helvetica, Arial, sans-serif; }"
                                  "h3 "
                                  "{ margin-left: 2em; }"
                                  "table "
                                  "{ margin-left: 1em; } "
                                  "caption "
                                  "{ text-align: left; font-weight: bold; font-size: 150%; border-bottom: .15em solid "
                                  "#4ca; margin-bottom: .5em;} "
                                  "th, td "
                                  "{ text-align: left; vertical-align: top; padding: 1px; }"
                                  "th "
                                  "{ min-width: 7em; width: 15%; } "
                                  ".username, .password, .url, .attr "
                                  "{ font-size: larger; font-family: monospace; } "
                                  ".notes "
                                  "{ font-size: small; } "
                                  "</style>"
                                  "</head>\n"
                                  "<body>"
                                  "<h1>"
                                + meta->name().toHtmlEscaped()
                                + "</h1>"
                                  "<p>"
                                + meta->description().toHtmlEscaped().replace("\n", "<br>")
                                + "</p>"
                                  "<p><code>"
                                + db->filePath().toHtmlEscaped() + "</code></p>");
    const auto footer = QString("</body>"
                                "</html>");

    if (device->write(header.toUtf8()) == -1) {
        m_error = device->errorString();
        return false;
    }

    if (db->rootGroup()) {
        if (!writeGroup(*device, *db->rootGroup(), QString(), sorted, ascending)) {
            return false;
        }
    }

    if (device->write(footer.toUtf8()) == -1) {
        m_error = device->errorString();
        return false;
    }

    return true;
}

bool HtmlExporter::writeGroup(QIODevice& device, const Group& group, QString path, bool sorted, bool ascending)
{
    // Don't output the recycle bin
    if (&group == group.database()->metadata()->recycleBin()) {
        return true;
    }

    if (!path.isEmpty()) {
        path.append(" &rarr; ");
    }
    path.append(group.name().toHtmlEscaped());

    // Output the header for this group (but only if there are
    // any notes or  entries in this group, otherwise we'd get
    // a header with nothing after it, which looks stupid)
    const auto notes = group.notes();
    if (!group.entries().empty() || !notes.isEmpty()) {
        // Header line
        auto header = QString("<hr><h2>");
        header.append(PixmapToHTML(Icons::groupIconPixmap(&group, IconSize::Medium)));
        header.append("&nbsp;");
        header.append(path);
        header.append("</h2>\n");

        // Group notes
        if (!notes.isEmpty()) {
            header.append("<p>");
            header.append(notes.toHtmlEscaped().replace("\n", "<br>"));
            header.append("</p>");
        }

        // Output it
        if (device.write(header.toUtf8()) == -1) {
            m_error = device.errorString();
            return false;
        }
    }

    // Begin the table for the entries in this group
    auto table = QString("<table width=\"95%\">");

    auto entries = group.entries();
    if (sorted) {
        std::sort(entries.begin(), entries.end(), [&](Entry* lhs, Entry* rhs) {
            int cmp = lhs->title().compare(rhs->title(), Qt::CaseInsensitive);
            return ascending ? cmp < 0 : cmp > 0;
        });
    }

    // Output the entries in this group
    for (const auto* entry : entries) {
        auto formatted_entry = formatEntry(*entry);

        if (formatted_entry.isEmpty())
            continue;

        // Output it into our table. First the left side with
        // icon and entry title ...
        table += "<tr>";
        table += "<td width=\"1%\">" + PixmapToHTML(Icons::entryIconPixmap(entry, IconSize::Medium)) + "</td>";
        auto caption = "<caption>" + entry->title().toHtmlEscaped() + "</caption>";

        // ... then the right side with the data fields
        table +=
            "<td style=\"padding-bottom: 0.5em;\"><table width=\"100%\">" + caption + formatted_entry + "</table></td>";
        table += "</tr>";
    }

    // Output the complete table of this group
    table.append("</table>\n");
    if (device.write(table.toUtf8()) == -1) {
        m_error = device.errorString();
        return false;
    }

    auto children = group.children();
    if (sorted) {
        std::sort(children.begin(), children.end(), [&](Group* lhs, Group* rhs) {
            int cmp = lhs->name().compare(rhs->name(), Qt::CaseInsensitive);
            return ascending ? cmp < 0 : cmp > 0;
        });
    }

    // Recursively output the child groups
    for (const auto* child : children) {
        if (child && !writeGroup(device, *child, path, sorted, ascending)) {
            return false;
        }
    }

    return true;
}
