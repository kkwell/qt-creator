// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "easyboardsettings.h"
#include "easyboardtr.h"

#include <coreplugin/icore.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/persistentsettings.h>
#include <utils/layoutbuilder.h>

using namespace Utils;

namespace EasyBoard::Internal {

const char DeviceManagerKey[] = "EasyBoardManager";
const char DeviceListKey[] = "BoardList";

const char EasyBoardFileName[] = "easyboard.xml";
const char EasyBoarddocType[]  = "EasyBoardDevices";

class EasyBoardSettingsPrivate
{
public:
    EasyBoardSettingsPrivate() = default;

    // int indexForId(Id id) const
    // {
    //     for (int i = 0; i < devices.count(); ++i) {
    //         if (devices.at(i)->id() == id)
    //             return i;
    //     }
    //     return -1;
    // }

    // QList<IDevice::Ptr> deviceList() const
    // {
    //     QMutexLocker locker(&mutex);
    //     return devices;
    // }

    static EasyBoardSettings *clonedInstance;

    mutable QMutex mutex;
    Boards *devices = nullptr;
    // QList<IDevice::Ptr> devices;
    // QHash<Id, Id> defaultDevices;
    PersistentSettingsWriter *writer = nullptr;
};

EasyBoardSettings *EasyBoardSettingsPrivate::clonedInstance = nullptr;
EasyBoardSettings *EasyBoardSettings::m_instance = nullptr;

EasyBoardSettings::EasyBoardSettings():
    d(new EasyBoardSettingsPrivate)
{
    m_instance = this;

    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested,
            this, &EasyBoardSettings::save);
}

EasyBoardSettings::~EasyBoardSettings()
{
    if (d->clonedInstance != this)
        delete d->writer;
    if (m_instance == this)
        m_instance = nullptr;
}

void EasyBoardSettings::setBoards(Boards *ptr)
{
    d->devices = ptr;
}

EasyBoardSettings *EasyBoardSettings::instance()
{
    return m_instance;
}

// EasyBoardSettings *EasyBoardSettings::cloneInstance()
// {
//     QTC_ASSERT(!EasyBoardSettingsPrivate::clonedInstance, return nullptr);

//     EasyBoardSettingsPrivate::clonedInstance = new EasyBoardSettings();
//     copy(instance(), EasyBoardSettingsPrivate::clonedInstance, true);
//     return EasyBoardSettingsPrivate::clonedInstance;
// }

EasyBoardSettings *EasyBoardSettings::clonedInstance()
{
    return EasyBoardSettingsPrivate::clonedInstance;
}


void EasyBoardSettings::copy(const EasyBoardSettings *source, EasyBoardSettings *target)
{
    target->d->devices = source->d->devices;
    // target->d->defaultDevices = source->d->defaultDevices;
}

Store EasyBoardSettings::board2Map(const Board &board) const
{
    Store map;

    map.insert("compatVersion",board.compatVersion);
    map.insert("copyright"    ,board.copyright    );
    map.insert("id"           ,board.id           );
    map.insert("license"      ,board.license      );
    map.insert("name"         ,board.name         );
    map.insert("displayName"  ,board.displayName  );
    map.insert("ip"           ,board.ip           );
    map.insert("version"      ,board.version      );
    map.insert("date"         ,board.date         );
    map.insert("type"         ,board.type==ItemTypeLocal?0:1);
    map.insert("isDefault"    ,board.isDefault    );

    return map;
}

Store EasyBoardSettings::toMap() const
{
    Store map;
    // Store defaultDeviceMap;
    // for (auto it = d->defaultDevices.constBegin(); it != d->defaultDevices.constEnd(); ++it)
    //     defaultDeviceMap.insert(keyFromString(it.key().toString()), it.value().toSetting());

    // map.insert(DefaultDevicesKey, variantFromStore(defaultDeviceMap));
    QVariantList deviceList;
    for (const Board &device : std::as_const(*d->devices))
        deviceList << variantFromStore(board2Map(device));
    map.insert(DeviceListKey, deviceList);
    return map;
}

void EasyBoardSettings::save()
{
    if (d->clonedInstance == this || !d->writer)
        return;
    Store data;
    data.insert(DeviceManagerKey, variantFromStore(toMap()));
    d->writer->save(data, Core::ICore::dialogParent());
}

static FilePath settingsFilePath(const QString &extension)
{
    return Core::ICore::userResourcePath(extension);
}

static FilePath systemSettingsFilePath(const QString &deviceFileRelativePath)
{
    return Core::ICore::installerResourcePath(deviceFileRelativePath);
}

void EasyBoardSettings::fromMap(const Store &map, Boards *settingDevices)
{
    const QVariantList deviceList = map.value(DeviceListKey).toList();
    for (const QVariant &v : deviceList) {
        const Store map = storeFromVariant(v);
        Board temp;
        temp.compatVersion = map.value("compatVersion").toString();
        temp.copyright     = map.value("copyright"    ).toString();
        temp.id            = map.value("id"           ).toString();
        temp.license       = map.value("license"      ).toString();
        temp.name          = map.value("name"         ).toString();
        temp.displayName   = map.value("displayName"  ).toString();
        temp.ip            = map.value("ip"           ).toString();
        temp.version       = map.value("version"      ).toString();
        temp.date          = map.value("date"         ).toString();
        temp.type          = map.value("type"         ).toInt()==0?ItemTypeLocal:ItemTypeNetwork;
        temp.isDefault     = map.value("isDefault"    ).toBool();
        temp.online = false;
        settingDevices->append(temp);
    }
}

void EasyBoardSettings::load()
{
    QTC_ASSERT(!d->writer, return);

    // Only create writer now: We do not want to save before the settings were read!
    d->writer = new PersistentSettingsWriter(settingsFilePath(EasyBoardFileName), EasyBoarddocType);

    PersistentSettingsReader reader;
    // read devices file from global settings path
    // QHash<Id, Id> defaultDevices;
    // QList<IDevice::Ptr> sdkDevices;
    // if (reader.load(systemSettingsFilePath(EasyBoardFileName)))
    //     sdkDevices = fromMap(storeFromVariant(reader.restoreValues().value(DeviceManagerKey)), &defaultDevices);
    // read devices file from user settings path

    if (reader.load(settingsFilePath(EasyBoardFileName)))
        fromMap(storeFromVariant(reader.restoreValues().value(DeviceManagerKey)), d->devices);
    // Insert devices into the model. Prefer the higher device version when there are multiple
    // devices with the same id.
    // for (IDevice::ConstPtr device : std::as_const(userDevices)) {
    //     for (const IDevice::Ptr &sdkDevice : std::as_const(sdkDevices)) {
    //         if (device->id() == sdkDevice->id() || device->rootPath() == sdkDevice->rootPath()) {
    //             if (device->version() < sdkDevice->version())
    //                 device = sdkDevice;
    //             sdkDevices.removeOne(sdkDevice);
    //             break;
    //         }
    //     }
    //     addDevice(device);
    // }
    emit devicesLoaded();
}

} // EasyBoard::Internal
