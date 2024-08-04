// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QAbstractListModel>

namespace ExtensionSystem {
class PluginSpec;
}

namespace EasyBoard::Internal {

using QPairList = QList<QPair<QString, QString> >;

using ImagesData = QPairList; // { <caption, url>, ... }
using LinksData = QPairList; // { <name, url>, ... }
using PluginsData = QPairList; // { <name, url>, ... }
using TextData = QList<QPair<QString, QStringList> >; // { <header, text>, ... }

enum ItemType {
    ItemTypePack,
    ItemTypeExtension,
};

enum BoardState {
    None, // Not a board
    Online,
    Offline,
};

enum Role {
    RoleName = Qt::UserRole,
    RoleCompatVersion,
    RoleCopyright,
    RoleDependencies,
    RoleDescriptionImages,
    RoleDescriptionLinks,
    RoleDescriptionText,
    RoleDownloadCount,
    RoleBoardState,
    RoleId,
    RoleItemType,
    RoleLicense,
    RoleLocation,
    RolePlatforms,
    RolePlugins,
    RoleSearchText,
    RoleSize,
    RoleTags,
    RoleVendor,
    RoleVersion,
};

class EasyBoardModel : public QAbstractListModel
{
public:
    EasyBoardModel(QObject *parent = nullptr);
    ~EasyBoardModel();

    int rowCount(const QModelIndex &parent = {}) const;
    QVariant data(const QModelIndex &index, int role) const;

    void setExtensionsJson(const QByteArray &json);

private:
    class EasyBoardModelPrivate *d = nullptr;
};

// ExtensionSystem::PluginSpec *pluginSpecForName(const QString &pluginName);

#ifdef WITH_TESTS
QObject *createExtensionsModelTest();
#endif

} // EasyBoard::Internal

Q_DECLARE_METATYPE(EasyBoard::Internal::QPairList)
Q_DECLARE_METATYPE(EasyBoard::Internal::TextData)
