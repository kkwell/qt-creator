// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <easyboard_export.h>
#include <easyboardstruct.h>
#include <utils/store.h>
#include <QObject>

#include <memory>

namespace Utils { class FilePath; }

namespace EasyBoard::Internal {

// class EasyBoardSettingsPrivate;

using namespace Utils;

class EASYBOARD_EXPORT EasyBoardSettings : public QObject
{
    Q_OBJECT

public:
    EasyBoardSettings();
    ~EasyBoardSettings() override;

    static EasyBoardSettings *instance();
    static EasyBoardSettings *clonedInstance();

    void setBoards(Boards *ptr);

    void save();
    void load();

    void addDevice(const Board &device);
    void removeDevice(const QString &id);

signals:

    void devicesLoaded(); // Emitted once load() is done

private:
    static void copy(const EasyBoardSettings *source, EasyBoardSettings *target);
    Store toMap() const;
    Store board2Map(const Board &board) const;

    void fromMap(const Store &map, Boards *settingDevices);

    class EasyBoardSettingsPrivate *d = nullptr;
    // EasyBoardSettingsPrivate d;

    static EasyBoardSettings *m_instance;
};

} // ExtensionManager::Internal
