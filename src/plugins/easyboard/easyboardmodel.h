// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QAbstractListModel>

namespace EasyBoard::Internal {

using QPairList = QList<QPair<QString, QString> >;

using ImagesData = QPairList; // { <caption, url>, ... }
using LinksData = QPairList; // { <name, url>, ... }
using PluginsData = QPairList; // { <name, url>, ... }
using TextData = QList<QPair<QString, QStringList> >; // { <header, text>, ... }

enum ItemType {
    ItemTypeLocal,
    ItemTypeNetwork,
};

enum BoardState {
    None, // Not a board
    Online,
    Offline,
};



class EasyBoardModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role{
        RoleName = Qt::UserRole,
        RoleDisplayName,
        RoleCompatVersion,
        RoleCopyright,
        RoleDependencies,
        RoleDescriptionImages,
        RoleDescriptionLinks,
        RoleDescriptionText,
        RoleBoardState,
        RoleIp,
        RoleId,
        RoleState,
        RoleItemType,
        RoleLicense,
        RoleLocation,
        RoleDate,
        RoleSearchText,
        RoleVersion,
        RoleDefault,
    };

    EasyBoardModel(QObject *parent = nullptr);
    ~EasyBoardModel();

    int rowCount(const QModelIndex &parent = {}) const;
    QVariant data(const QModelIndex &index, int role) const;

    void setBoards(const QByteArray &json);

    void onSocketData(QJsonObject str);

public slots:
    void removeFromList(const QString &id,const ItemType &itemType);
    void setDefault(const QString &id,const ItemType &itemType);

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
