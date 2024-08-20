// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "easyboardmodel.h"
#include "easyboardsettings.h"

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
        board.id = "x1";
        parsedBoards.append(board);
        board.name = QString("local config test3");
        board.ip = "1.0.0.2";
        board.date = "2000-08-08 19:01:30:011";
        board.id = "x2";
        parsedBoards.append(board);
        board.name = QString("local config test0");
        board.ip = "1.0.0.3";
        board.date = "1990-08-08 19:01:10:911";
        board.id = "x3";
        parsedBoards.append(board);


        board.name = "T113-S3";
        board.date = "2024-08-08 20:01:50:312";
        board.id = "T012093109230";
        board.ip = "192.168.3.98";
        board.version = "V1.0.0";
        board.type = ItemTypeNetwork;
        board.isDefault = true;
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
    EasyBoardModelPrivate(){
        // qDebug()<<"EasyBoardModelPrivate";
        boardSettings.setBoards(&boards);
        // qDebug()<<"start loading";
        boardSettings.load();
        // qDebug()<<"EasyBoardModelPrivate end";

    };

    void setBoards(const Boards &boards);
    void addUnlistedLocalBoards();
    void updateBoard(Board &board);
    void remove();

    void getBoards();

    EasyBoardSettings boardSettings;

    Boards boards;
};

void EasyBoardModelPrivate::getBoards()
{
    // QtcSettings *s = ICore::settings();
    // const QStringList deviceIds = s->value(BOARDS_EXISTENCE_IDS).toStringList();
    // qDebug()<<"deviceIds:"<<deviceIds;
    // const QHash<QString, QVariant> boardsList
    //     = s->value(BOARDS_EXISTENCE_KEY).toHash();
    // if(deviceIds.isEmpty())
    //     return;
    // qDebug()<<deviceIds.size()<<boardsList.size();

    // for (int i = 0; i < deviceIds.size(); ++i) {
    //     const QJsonObject exists = boardsList.value(deviceIds.at(i), QJsonObject()).toJsonObject();
    //     qDebug()<<deviceIds.at(i)<<exists;
    // }
}


void EasyBoardModelPrivate::remove()
{
    for(Board temp:boards){
        qDebug()<<temp.name;
    }
}


void EasyBoardModelPrivate::updateBoard(Board &board)
{
    for (Board &board_temp:boards) {
        if(board_temp.type==ItemTypeLocal)
            continue;
        if(board_temp.id==board.id){
            //update
            board.isDefault = board_temp.isDefault;
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
    connect(&d->boardSettings,SIGNAL(devicesLoaded()),this,SLOT(devicesLoaded()));
}

EasyBoardModel::~EasyBoardModel()
{
    d->boardSettings.save();
    delete d;
}

Board *EasyBoardModel::getIndexBoard(const QModelIndex &index)
{
    return (Board *)&d->boards.at(index.row());
}

void EasyBoardModel::save()
{
    // d->saveBoards();
}

void EasyBoardModel::read()
{
    d->getBoards();
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
    case EasyBoardModel::RoleName:
        return board.name;
    case EasyBoardModel::RoleDisplayName:
        return board.displayName;
    case EasyBoardModel::RoleCompatVersion:
        return board.compatVersion;
    case EasyBoardModel::RoleCopyright:
        return !board.copyright.isEmpty() ? board.copyright : QVariant();
    case EasyBoardModel::RoleDependencies:
        return QVariant();//dependenciesFromExtension(board);
    case EasyBoardModel::RoleDescriptionImages:
        // return QVariant::fromValue(board.description.images);
    case EasyBoardModel::RoleDescriptionLinks:
        // return QVariant::fromValue(board.description.links);
    case EasyBoardModel::RoleDescriptionText:
        return QVariant();
    case EasyBoardModel::RoleIp:
        return board.ip;
    case EasyBoardModel::RoleId:
        return board.id;
    case EasyBoardModel::RoleItemType:
        return board.type;
    case EasyBoardModel::RoleLicense:
        return board.license;
    case EasyBoardModel::RoleState:
        return board.online;
    case EasyBoardModel::RoleDate:
        return board.date;
    case EasyBoardModel::RoleDefault:
        return board.isDefault;
        break;
    case EasyBoardModel::RoleVersion:
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
    return index.data(EasyBoardModel::RoleState).toBool()?Online:Offline;

    // const PluginSpec *ps = pluginSpecForName(index.data(RoleName).toString());
    // if (!ps)
    //     return Online;

    // return ps->isEffectivelyEnabled() ? Online : Offline;
    // return Online;
}

static QString searchText(const QModelIndex &index)
{
    QStringList searchTexts;
    searchTexts.append(index.data(EasyBoardModel::RoleName).toString());
    searchTexts.append(index.data(EasyBoardModel::RoleIp).toStringList());
    searchTexts.append(index.data(EasyBoardModel::RoleDescriptionText).toStringList());
    searchTexts.append(index.data(EasyBoardModel::RoleVersion).toString());
    return searchTexts.join(" ");
}

QVariant EasyBoardModel::data(const QModelIndex &index, int role) const
{
    if (role == RoleBoardState)
        return boardState(index);
    if (role == RoleSearchText)
        return searchText(index);

    const Board &board = d->boards.at(index.row());
    const QVariant boardData = dataFromBoard(board,role);// = dataFromExtension(extension, role);
    // If data is unavailable, retrieve it from the first contained plugin
    // if (extensionData.isNull() && !extension.plugins.isEmpty()) {
    //     const QString firstPluginName = extension.plugins.constFirst().name;
    //     const Extension firstPluginExtension =
    //         findOrDefault(d->extensions, Utils::equal(&Extension::name, firstPluginName));
    //     if (firstPluginExtension.name.isEmpty())
    //         return {};
    //     return dataFromExtension(firstPluginExtension, role);
    // }
    return boardData;
}

void EasyBoardModel::onSocketData(QJsonObject str)
{
    if(str.value("TYPE").toString().contains("udp",Qt::CaseInsensitive)){
        emit udpResult(str);
        return;
    }

    Board board;
    board.name = str.value("HOST").toString();
    board.date = str.value("DATE").toString();
    board.id = str.value("ID").toString();
    board.ip = str.value("IP").toString();
    board.version = str.value("VERSION").toString();
    board.type = ItemTypeNetwork;
    board.online = true;
    // beginResetModel();
    // d->updateBoard(board);

    for (int i=0;i<d->boards.size();i++) {
        if(d->boards.at(i).type==ItemTypeNetwork){
            if(d->boards.at(i).id==board.id){
                //update
                board.isDefault = d->boards.at(i).isDefault;
                board.displayName = d->boards.at(i).displayName;
                d->boards[i] = board;
                emit dataChanged(index(i), index(i));
                emit dataChange();
                return;
            }
        }
    }

    beginInsertRows(QModelIndex(),d->boards.size(),d->boards.size());
    d->boards.append(board);
    endInsertRows();
    emit dataChange();
    // ui->plainTextEdit->appendPlainText("ip:"+str.value("IP").toString());
    // ui->plainTextEdit->appendPlainText("id:"+str.value("ID").toString());
}

void EasyBoardModel::devicesLoaded()
{
    beginResetModel();
    endResetModel();
    emit dataChange();
}

bool EasyBoardModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (index.isValid() && role == Qt::EditRole) {

        emit dataChanged(index, index, {role});
        return true;
    }
    return false;
}

void EasyBoardModel::setBoards(const QByteArray &json)
{
    const Boards boards = parseBoardsRepoReply(json);
    beginResetModel();
    d->setBoards(boards);
    endResetModel();
    emit dataChange();
}

void EasyBoardModel::removeFromList(const QModelIndex &idx)
{
    // const QModelIndex magicIndex = boardsView->currentIndex();
    QTC_ASSERT(idx.isValid(), return);

    beginRemoveRows(QModelIndex(),idx.row(),idx.row());
    // d->boards.removeAt(magicIndex.row());
    for (int i=0;i<d->boards.size();i++) {
        if(i==idx.row()){
            d->boards.removeAt(i);
            break;
        }
    }
    endRemoveRows();
    emit dataChange();
}

void EasyBoardModel::setDefault(const QModelIndex &idx)
{
    QTC_ASSERT(idx.isValid(), return);
    for (int i=0;i<d->boards.size();i++) {
        if(i==idx.row()){
            d->boards[i].isDefault = true;
            emit dataChanged(idx, idx);
        }else{
            if(d->boards[i].isDefault){
                d->boards[i].isDefault = false;
                emit dataChanged(index(i), index(i));
            }
        }
    }

    emit dataChange();
}

const QModelIndex EasyBoardModel::getBoardModelIndex(const QString &idx,const ItemType &type)
{
    for (int i=0;i<d->boards.size();i++) {
        if(d->boards[i].id==idx && d->boards[i].type==type)
            return index(i);
    }
    return QModelIndex();
}

bool EasyBoardModel::haveSameConfig(const QString &idx,const ItemType &type)
{
    for (int i=0;i<d->boards.size();i++) {
        if(d->boards[i].id==idx && d->boards[i].type==type)
            return true;
    }
    return false;
}

bool EasyBoardModel::isHaveDefault()
{
    for (int i=0;i<d->boards.size();i++) {
        if(d->boards[i].isDefault)
            return true;
    }
    return false;
}
// void EasyBoardModel::updateIndex(const QModelIndex &index)
// {
//     for (int i=0;i<d->boards.size();i++) {
//         if(id==d->boards.at(i).id){
//             emit dataChanged(index(i), index(i));
//             return;
//         }
//     }
// }

void EasyBoardModel::addNewBoard(const Board &mBoard)
{
    Board temp;
    temp.displayName = mBoard.displayName;
    temp.ip = mBoard.ip;
    beginInsertRows(QModelIndex(),d->boards.size(),d->boards.size());
    d->boards.append(temp);
    endInsertRows();
    emit dataChange();
}

void EasyBoardModel::debugTest(const QModelIndex &index)
{
    if(index.isValid()){
        qDebug()<<d->boards[index.row()].name
                <<d->boards[index.row()].displayName
                <<d->boards[index.row()].date
                <<d->boards[index.row()].ip
                 <<d->boards[index.row()].id;
    }
    else{
        qDebug()<<"QModelIndex is not valid";
    }

}

} // BoardManager::Internal
