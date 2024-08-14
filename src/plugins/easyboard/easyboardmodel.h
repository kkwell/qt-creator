// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QAbstractListModel>
#include <easyboardstruct.h>
#include <QListView>

namespace EasyBoard::Internal {

using QPairList = QList<QPair<QString, QString> >;

using ImagesData = QPairList; // { <caption, url>, ... }
using LinksData = QPairList; // { <name, url>, ... }
using PluginsData = QPairList; // { <name, url>, ... }
using TextData = QList<QPair<QString, QStringList> >; // { <header, text>, ... }



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

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void setBoards(const QByteArray &json);
    Board *getIndexBoard(const QString &id,const ItemType &itemType);
    void updateIndex(const QString &id);

    void addNewBoard(const Board &);

    void onSocketData(QJsonObject str);
    void save();
    void read();
    void setListView(QListView *view);

public slots:
    void removeFromList(const QString &id,const ItemType &itemType);
    void setDefault(const QString &id,const ItemType &itemType);
    void devicesLoaded();

signals:
    void dataChange();

private:
    QListView *boardsView;
    class EasyBoardModelPrivate *d = nullptr;
};

// ExtensionSystem::PluginSpec *pluginSpecForName(const QString &pluginName);

#ifdef WITH_TESTS
QObject *createExtensionsModelTest();
#endif

} // EasyBoard::Internal

Q_DECLARE_METATYPE(EasyBoard::Internal::QPairList)
Q_DECLARE_METATYPE(EasyBoard::Internal::TextData)
