#ifndef NETPROPERTY_H
#define NETPROPERTY_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QVariant>
#include <QCryptographicHash>
#include <QJsonObject>
#include "qaesencryption.h"

#define BROADCAST "239.255.255.250"
#define BROADCASTPORT 1901

class netproperty : public QObject
{
    Q_OBJECT
public:
    explicit netproperty(QObject *parent = nullptr);
    ~netproperty();

    // void bindAllNet();
    void exitAllNet();

    void findEasyBoard();//发送组播消息
    void connectEasyBoard(const QString ip,const quint16 port);//连接开发板

    int bindAllNet();

    QString TypeToQString(int type);
    QString FlagsToQString(int flags);

    bool NetInterfaceIsUseful(int flags);
    void sendbroadcast(QByteArray msg);
    void sendUdp(const QString &ip,const quint16 &port,QByteArray msg);

    QByteArray encodedText(QByteArray data); //加密
    QByteArray decodedText(QByteArray data); //解密

signals:
    void getSocketData(QJsonObject str);
    // void getUdpData(QJsonObject str);

private slots:
    void onSocketReadyRead(); // 读取socket传入的数据
    void processPendingDatagrams();
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
    QUdpSocket *udpSocket;
    QHostAddress udpAddress;
    quint16 udpPort;
};

#endif // NETPROPERTY_H
