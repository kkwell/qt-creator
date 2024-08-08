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

struct Description {
    ImagesData images;
    LinksData links;
    TextData text;
};

struct Board {
    QString compatVersion;
    QString copyright;
    Description description;
    QString id;
    QString license;
    QString name;
    QString ip;
    QString version;
    ItemType type;
    QString date;
    bool online;
};
using Boards = QList<Board>;

static Boards parseBoardsRepoReply(const QByteArray &jsonData)
{
    // https://qc-extensions.qt.io/api-docs
    Boards parsedBoards;

        Board board;// = extensionFromJson(itemObj);
        board.name = QString("local config test6");
        board.copyright = QString("CopyRight KK");
        board.ip = "1.0.0.1";
        board.date = "2024-08-08 19:01:50:012";
        board.online = false;
        board.type = ItemTypeLocal;
        parsedBoards.append(board);
        board.name = QString("local config test3");
        board.ip = "1.0.0.2";
        board.date = "2000-08-08 19:01:30:011";
        parsedBoards.append(board);
        board.name = QString("local config test0");
        board.ip = "1.0.0.3";
        board.date = "1990-08-08 19:01:10:911";
        parsedBoards.append(board);


        board.name = "T113-S3";
        board.date = "2024-08-08 20:01:50:312";
        board.id = "T012093109230";
        board.ip = "192.168.3.98";
        board.version = "V1.0.0";
        board.type = ItemTypeNetwork;
        board.online = true;
        parsedBoards.append(board);

        board.name = "T113-S3";
        board.date = "1900-01-01 20:01:50:312";
        board.id = "T012093109231";
        board.ip = "192.168.3.99";
        board.version = "V1.0.0";
        board.type = ItemTypeNetwork;
        board.online = true;
        parsedBoards.append(board);

    return parsedBoards;
}

class EasyBoardModelPrivate
{
public:
    void setBoards(const Boards &boards);
    void addUnlistedLocalBoards();
    void updateBoard(const Board &board);
    Boards boards;
};


void EasyBoardModelPrivate::updateBoard(const Board &board)
{
    for (Board &board_temp:boards) {
        if(board_temp.id==board.id){
            //update
            board_temp = board;
            return;
        }
    }
    boards.append(board);
}

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
    case RoleIp:
        return board.ip;
    case RoleItemType:
        return board.type;
    case RoleLicense:
        return board.license;
    case RoleState:
        return board.online;
    case RoleDate:
        return board.date;
        break;
    case RoleVersion:
        return !board.version.isEmpty() ? board.version : QVariant();
    default:
        break;
    }
    return {};
}

BoardState boardState(const QModelIndex &index)
{
    // if (index.data(RoleItemType) != ItemTypeLocal)
    //     return None;
    return index.data(RoleState).toBool()?Online:Offline;

    // const PluginSpec *ps = pluginSpecForName(index.data(RoleName).toString());
    // if (!ps)
    //     return Online;

    // return ps->isEffectivelyEnabled() ? Online : Offline;
    // return Online;
}

static QString searchText(const QModelIndex &index)
{
    QStringList searchTexts;
    searchTexts.append(index.data(RoleName).toString());
    searchTexts.append(index.data(RoleIp).toStringList());
    searchTexts.append(index.data(RoleDescriptionText).toStringList());
    searchTexts.append(index.data(RoleVersion).toString());
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
    Board board;
    board.name = str.value("HOST").toString();
    board.date = str.value("DATE").toString();
    board.id = str.value("ID").toString();
    board.ip = str.value("IP").toString();
    board.version = str.value("VERSION").toString();
    board.type = ItemTypeNetwork;
    beginResetModel();
    d->updateBoard(board);
    endResetModel();
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
