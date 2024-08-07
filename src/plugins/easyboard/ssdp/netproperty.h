#ifndef NETPROPERTY_H
#define NETPROPERTY_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QVariant>
#include <QCryptographicHash>
#include <QJsonObject>
#include "qaesencryption.h"

class netproperty : public QObject
{
    Q_OBJECT
public:
    explicit netproperty(QObject *parent = nullptr);
    ~netproperty();

    int bindAllNet();
    void exitAllNet();

    QString TypeToQString(int type);
    QString FlagsToQString(int flags);

    bool NetInterfaceIsUseful(int flags);
    void sendbroadcast(QByteArray msg);

    QByteArray encodedText(QByteArray data); //加密
    QByteArray decodedText(QByteArray data); //解密

    void findEasyBoard();//发送组播消息
signals:
    void getSocketData(QJsonObject str);

private slots:
    void onSocketReadyRead(); // 读取socket传入的数据

private:
    QString groupIp;
    quint16 groupPort;
    QList<QUdpSocket *> m_udpSocketlist;
    QStringList m_localIpList;
    QString m_key;
    QString m_iv;
    QByteArray m_hashKey;
    QByteArray m_hashIV;
    QAESEncryption *p_AES;
};

#endif // NETPROPERTY_H
