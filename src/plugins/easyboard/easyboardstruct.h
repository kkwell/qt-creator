#ifndef EASYBOARDSTRUCT_H
#define EASYBOARDSTRUCT_H

namespace EasyBoard::Internal {

enum ItemType {
    ItemTypeLocal,
    ItemTypeNetwork,
};

enum BoardState {
    None, // Not a board
    Online,
    Offline,
};

struct Board {
    QString compatVersion;
    QString copyright;
    QString id;
    QString license;
    QString name;
    QString displayName;
    QString ip;
    QString version;
    QString date;
    ItemType type = ItemTypeLocal;
    bool isDefault = false;
    bool online = false;
};

using Boards = QList<Board>;

}
#endif // EASYBOARDSTRUCT_H
