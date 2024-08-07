// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "easyboardmodel.h"

#include "utils/algorithm.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginspec.h>
#include <extensionsystem/pluginview.h>
#include <extensionsystem/pluginmanager.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardItemModel>
#include <QVersionNumber>
#include <QLoggingCategory>

using namespace ExtensionSystem;
using namespace Core;
using namespace Utils;

namespace EasyBoard::Internal {

Q_LOGGING_CATEGORY(modelLog, "qtc.easyboard.model", QtWarningMsg)

// struct Dependency
// {
//     QString name;
//     QString version;
// };
// using Dependencies = QList<Dependency>;

// struct Plugin
// {
//     QString copyright;
//     Dependencies dependencies;
//     bool isInternal = false;
//     QString name;
//     QString packageUrl;
//     QString vendor;
//     QString version;
// };
// using Plugins = QList<Plugin>;

struct Description {
    ImagesData images;
    LinksData links;
    TextData text;
};

struct Board {
    QString compatVersion;
    QString copyright;
    Description description;
    int downloadCount = -1;
    QString id;
    QString license;
    QString name;
    QStringList platforms;
    qint64 size = 0;
    QStringList tags;
    ItemType type = ItemTypePack;
    QString vendor;
    QString version;
};
using Boards = QList<Board>;

static Boards parseBoardsRepoReply(const QByteArray &jsonData)
{
    // https://qc-extensions.qt.io/api-docs
    Boards parsedBoards;


    for(int i=0;i<3;i++){
        Board board;// = extensionFromJson(itemObj);
        board.name = QString("test [%1]").arg(i);
        board.copyright = QString("copyright %1").arg(i);
        parsedBoards.append(board);
    }
    // const QJsonObject jsonObj = QJsonDocument::fromJson(jsonData).object();
    // const QJsonArray items = jsonObj.value("items").toArray();
    // for (const QJsonValueConstRef &itemVal : items) {
    //     const QJsonObject itemObj = itemVal.toObject();
    //     const Board extension;// = extensionFromJson(itemObj);
    //     parsedBoards.append(extension);
    // }
    return parsedBoards;
}

class EasyBoardModelPrivate
{
public:
    void setBoards(const Boards &boards);
    void addUnlistedLocalBoards();

    Boards boards;
};

void EasyBoardModelPrivate::setBoards(const Boards &boards)
{
    this->boards = boards;
    qCDebug(modelLog) << "Number of boards from JSON:" << this->boards.count();
    addUnlistedLocalBoards();
    qCDebug(modelLog) << "Number of boards with added local ones:" << this->boards.count();
}

void EasyBoardModelPrivate::addUnlistedLocalBoards()
{
    const QStringList listedModelBoards = transform(boards, &Board::name);
    // for (const PluginSpec *plugin : PluginManager::plugins())
    //     if (!listedModelExtensions.contains(plugin->name()))
    //         extensions.append(extensionFromPluginSpec(plugin));
}

EasyBoardModel::EasyBoardModel(QObject *parent)
    : QAbstractListModel(parent)
    , d(new EasyBoardModelPrivate)
{
}

EasyBoardModel::~EasyBoardModel()
{
    delete d;
}

int EasyBoardModel::rowCount([[maybe_unused]] const QModelIndex &parent) const
{
    return d->boards.count();
}

// static QStringList dependenciesFromExtension(const Extension &extension)
// {
//     QStringList dependencies;
//     for (const Plugin &plugin : extension.plugins) {
//         for (const Dependency &dependency : plugin.dependencies) {
//             const QString withVersion = QString::fromLatin1("%1 (%2)").arg(dependency.name)
//             .arg(dependency.version);
//             dependencies.append(withVersion);
//         }
//     }
//     dependencies.sort();
//     dependencies.removeDuplicates();
//     return dependencies;
// }

static QVariant dataFromBoard(const Board &board, int role)
{
    switch (role) {
    case Qt::DisplayRole:
    case RoleName:
        return board.name;
    case RoleCompatVersion:
        return board.compatVersion;
    case RoleCopyright:
        return !board.copyright.isEmpty() ? board.copyright : QVariant();
    case RoleDependencies:
        return QVariant();//dependenciesFromExtension(board);
    case RoleDescriptionImages:
        return QVariant::fromValue(board.description.images);
    case RoleDescriptionLinks:
        return QVariant::fromValue(board.description.links);
    case RoleDescriptionText:
        return QVariant::fromValue(board.description.text);
    case RoleDownloadCount:
        return board.downloadCount;
    case RoleId:
        return board.id;
    case RoleItemType:
        return board.type;
    case RoleLicense:
        return board.license;
    case RoleLocation:
        break;
    case RolePlatforms:
        return board.platforms;
    // case RolePlugins: {
    //     PluginsData plugins;
    //     for (const Plugin &plugin : extension.plugins)
    //         plugins.append(qMakePair(plugin.name, plugin.packageUrl));
    //     return QVariant::fromValue(plugins);
    // }
    case RoleSize:
        return board.size;
    case RoleTags:
        return board.tags;
    case RoleVendor:
        return !board.vendor.isEmpty() ? board.vendor : QVariant();
    case RoleVersion:
        return !board.version.isEmpty() ? board.version : QVariant();
    default:
        break;
    }
    return {};
}

BoardState boardState(const QModelIndex &index)
{
    if (index.data(RoleItemType) != ItemTypeExtension)
        return None;

    // const PluginSpec *ps = pluginSpecForName(index.data(RoleName).toString());
    // if (!ps)
    //     return Online;

    // return ps->isEffectivelyEnabled() ? Online : Offline;
    return Online;
}

static QString searchText(const QModelIndex &index)
{
    QStringList searchTexts;
    searchTexts.append(index.data(RoleName).toString());
    searchTexts.append(index.data(RoleTags).toStringList());
    searchTexts.append(index.data(RoleDescriptionText).toStringList());
    searchTexts.append(index.data(RoleVendor).toString());
    return searchTexts.join(" ");
}

QVariant EasyBoardModel::data(const QModelIndex &index, int role) const
{
    if (role == RoleBoardState)
        return boardState(index);
    if (role == RoleSearchText)
        return searchText(index);

    const Board &board = d->boards.at(index.row());
    const QVariant extensionData = dataFromBoard(board,role);// = dataFromExtension(extension, role);
    // If data is unavailable, retrieve it from the first contained plugin
    // if (extensionData.isNull() && !extension.plugins.isEmpty()) {
    //     const QString firstPluginName = extension.plugins.constFirst().name;
    //     const Extension firstPluginExtension =
    //         findOrDefault(d->extensions, Utils::equal(&Extension::name, firstPluginName));
    //     if (firstPluginExtension.name.isEmpty())
    //         return {};
    //     return dataFromExtension(firstPluginExtension, role);
    // }
    return extensionData;
}

void EasyBoardModel::onSocketData(QJsonObject str)
{
    qDebug()<<str;
    // ui->plainTextEdit->appendPlainText("ip:"+str.value("IP").toString());
    // ui->plainTextEdit->appendPlainText("id:"+str.value("ID").toString());
}

void EasyBoardModel::setBoards(const QByteArray &json)
{
    const Boards boards = parseBoardsRepoReply(json);
    beginResetModel();
    d->setBoards(boards);
    endResetModel();
}

} // BoardManager::Internal
