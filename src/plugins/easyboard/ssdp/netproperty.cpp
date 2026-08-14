#include "netproperty.h"
#include <QDateTime>
#include <QTime>
#include <QNetworkInterface>
#include <QJsonDocument>

netproperty::netproperty(QObject *parent)
    : QObject{parent}
{
    groupIp = BROADCAST;
    groupPort = BROADCASTPORT;
    m_key = "kvell-easyboard-key-for-ssdp!";
    m_iv  = "your-IV-vector";
    // bindAllNet();
    p_AES = new QAESEncryption(QAESEncryption::AES_256, QAESEncryption::CBC);

    m_hashKey = QCryptographicHash::hash(m_key.toLocal8Bit(), QCryptographicHash::Sha256);
    m_hashIV = QCryptographicHash::hash(m_iv.toLocal8Bit(), QCryptographicHash::Md5);

    udpSocket = new QUdpSocket;
    udpSocket->bind(QHostAddress::Any, groupPort,QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    // udpSocket->setSocketOption(QAbstractSocket::MulticastTtlOption,1);
    // udpSocket->setSocketOption(QAbstractSocket::MulticastLoopbackOption,true);
    // connect(udpSocket,SIGNAL(readyRead()),this,SLOT(processPendingDatagrams()));
    // udpSocket->setMulticastInterface(network);//设置组播网卡
}

netproperty::~netproperty()
{
    delete udpSocket;
    exitAllNet();
    delete p_AES;
}

void netproperty::processPendingDatagrams()
{
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        udpSocket->readDatagram(datagram.data(), datagram.size(), &udpAddress, &udpPort);

        QJsonParseError json_error;

        QJsonDocument jsonDoc(QJsonDocument::fromJson(decodedText(datagram), &json_error));
        if(json_error.error != QJsonParseError::NoError){
            qDebug() << "json error!" << json_error.errorString();
            return;
        }
        qDebug()<<"udp:"<<jsonDoc;
        QJsonObject rootObj = jsonDoc.object();
        rootObj.insert("IP",udpAddress.toString());

        // emit getUdpData(rootObj);
    }
}

void netproperty::connectEasyBoard(const QString ip,const quint16 port)
{
    QDateTime dateTime= QDateTime::currentDateTime();//获取系统当前的时间
    QString str = dateTime.toString("yyyy-MM-dd hh:mm:ss:zzz");//格式化时间
    const int msg_length = 5;
    QStringList temp_msg[msg_length];
    temp_msg[0] << "HOST" << "easyboard";
    temp_msg[1] << "MAN"  << "ssdp:alive";
    temp_msg[2] << "DATE" << str;
    temp_msg[3] << "ID"   << "main";
    temp_msg[4] << "TYPE" << "udp";

    QJsonObject jsonObject;

    for(int i=0;i<msg_length;i++){
        jsonObject.insert(temp_msg[i].first(),temp_msg[i].last());
    }
    QJsonDocument jsonDocument;
    jsonDocument.setObject(jsonObject);

    exitAllNet();
    if(bindAllNet()==0){
        qDebug()<<"所有网络绑定失败";
        return;
    }

    sendUdp(ip,port,jsonDocument.toJson());
}

void netproperty::findEasyBoard()
{
    exitAllNet();
    if(bindAllNet()==0){
        qDebug()<<"所有网络绑定失败";
        return;
    }

    QDateTime dateTime= QDateTime::currentDateTime();//获取系统当前的时间
    QString str = dateTime.toString("yyyy-MM-dd hh:mm:ss:zzz");//格式化时间
    const int msg_length = 5;
    QStringList temp_msg[msg_length];
    temp_msg[0] << "HOST" << "easyboard";
    temp_msg[1] << "MAN"  << "ssdp:alive";
    temp_msg[2] << "DATE" << str;
    temp_msg[3] << "ID"   << "main";
    temp_msg[4] << "TYPE" << "broadcast";
    // QString temp_msg[msg_length];
    // QStringList msg;
    // temp_msg[0] = "HOST:easyboard";
    // temp_msg[1] = "MAN:\"ssdp:alive\"";
    // temp_msg[2] = "DATE:"+str;
    // temp_msg[3] = QString("ID:%1").arg("K011091910");

    // for(int i=0;i<msg_length;i++)
    // {
    //     QTime t;
    //     t=QTime::currentTime();
    //     srand(t.msec()+t.second()*1000);
    //     int r = i+rand()%(msg_length-i);
    //     msg.append(temp_msg[r]);
    //     temp_msg[r]=temp_msg[i];
    // }
    // QTime t;
    // t=QTime::currentTime();
    // srand(t.msec()+t.second()*1000);
    // int r = rand()%msg_length;

    QJsonObject jsonObject;

    for(int i=0;i<msg_length;i++){
        jsonObject.insert(temp_msg[i].first(),temp_msg[i].last());
    }
    QJsonDocument jsonDocument;
    jsonDocument.setObject(jsonObject);
    sendbroadcast(jsonDocument.toJson());

    // QByteArray datagram = encodedText(jsonDocument.toJson());
    // for (int i=0;i< m_udpSocketlist.size();i++) {
    //     m_udpSocketlist[i]->writeDatagram(datagram,QHostAddress("192.168.98.100"), groupPort);
    // }
}

int netproperty::bindAllNet()
{
    QList<QNetworkInterface> networkinterfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &network : networkinterfaces){
        // qDebug()<<"name:"<<networkinterfaces[i].name();
        // qDebug()<<FlagsToQString(networkinterfaces[i].flags());// 返回与此网络接口关联的标志

        // qDebug()<<TypeToQString(networkinterfaces[i].type()); // 获取网络类型说明
        // qDebug()<<networkinterfaces[i].hardwareAddress();

        if(NetInterfaceIsUseful(network.flags())){
            QList<QNetworkAddressEntry> addresses = network.addressEntries();
            for (const QNetworkAddressEntry &address : addresses)
            {
                QString strType;
                switch (address.ip().protocol())       // 判断IP地址类型
                {
                case QAbstractSocket::IPv4Protocol:
                    strType = "--------IPv4地址--------";
                    break;
                case QAbstractSocket::IPv6Protocol:
                    strType = "--------IPv6地址--------";
                    break;
                case QAbstractSocket::AnyIPProtocol:
                    strType = "--------IPv4或IPv6地址--------";
                    break;
                case QAbstractSocket::UnknownNetworkLayerProtocol:
                    strType = "--------未知地址--------";
                    break;
                }
                QString ipInfo = QString("IP地址：%1，子网掩码：%2，广播地址：%3").arg(address.ip().toString())
                                     .arg(address.netmask().toString())
                                     .arg(address.broadcast().toString());

                // qDebug()<<strType;// 显示IP地址类型
                // qDebug()<<ipInfo; // 显示IP地址信息

                QHostAddress broadcastAddress = address.broadcast();
                if (broadcastAddress != QHostAddress::Null
                    && address.ip() != QHostAddress::LocalHost
                    && address.ip().protocol() == QAbstractSocket::IPv4Protocol
                    )
                {

                    QUdpSocket *sock = new QUdpSocket();
                    if(sock->bind(QHostAddress::AnyIPv4, groupPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
                    {
                        qDebug() << "bind ok" << address.ip();
                        // Multicast路由层次，1表示只在同一局域网内
                        // 组播TTL: 生存时间，每跨1个路由会减1，多播无法跨过大多数路由所以为1
                        // 默认值是1，表示数据包只能在本地的子网中传送。
                        sock->setSocketOption(QAbstractSocket::MulticastTtlOption,1);
                        sock->setSocketOption(QAbstractSocket::MulticastLoopbackOption,true);
                        connect(sock,SIGNAL(readyRead()),this,SLOT(onSocketReadyRead()));
                        sock->setMulticastInterface(network);//设置组播网卡
                        // sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,1024*1024*8);//设置缓冲区
                        if(sock->joinMulticastGroup(QHostAddress(groupIp),network)){//加入组播
                            m_udpSocketlist.append(sock);
                            m_localIpList.append(address.ip().toString());
                        }
                        else{
                            qDebug()<<QString("加入组播失败:ip[%1],port[%2]").arg(address.ip().toString()).arg(groupPort);
                            delete sock;
                        }
                    }
                    else{
                        qDebug()<<QString("绑定端口失败:ip[%1],port[%2]").arg(address.ip().toString()).arg(groupPort);
                        delete sock;
                    }
                }

            }
        }
    }

    return m_udpSocketlist.size();
}

void netproperty::exitAllNet()
{
    if(m_udpSocketlist.isEmpty())
        return;

    for (int i=0;i< m_udpSocketlist.size();i++) {
        m_udpSocketlist[i]->leaveMulticastGroup(QHostAddress(groupIp));// 退出组播
        m_udpSocketlist[i]->abort();// 中止当前连接并重置套接字。与disconnectFromHost()不同，此函数会立即关闭套接字，丢弃写入缓冲区中的所有挂起数据。
    }
    qDeleteAll(m_udpSocketlist);
    m_udpSocketlist.clear();
    m_localIpList.clear();
}

void netproperty::sendUdp(const QString &ip,const quint16 &port,QByteArray msg)
{
    // QByteArray datagram = encodedText(msg);
    // udpSocket->writeDatagram(datagram, QHostAddress(ip), port);
    QByteArray datagram = encodedText(msg);
    for (int i=0;i< m_udpSocketlist.size();i++) {
        qDebug()<<i<<ip<<port;
        m_udpSocketlist[i]->writeDatagram(datagram,QHostAddress(ip), port);
    }
}

void netproperty::sendbroadcast(QByteArray msg)
{
    QByteArray datagram = encodedText(msg);
    for (int i=0;i< m_udpSocketlist.size();i++) {
        m_udpSocketlist[i]->writeDatagram(datagram,QHostAddress(groupIp), groupPort);
    }
    // udpSocket->writeDatagram(datagram,groupAddress,groupPort);
    // ui->plainTextEdit->appendPlainText("[multicst] "+msg);
}

void netproperty::onSocketReadyRead()
{
    QUdpSocket * sock = qobject_cast<QUdpSocket *>(this->sender());
    QHostAddress targetaddr;
    quint16 targetport;

    while (sock->hasPendingDatagrams())
    {
        QByteArray data;
        data.resize((int)sock->pendingDatagramSize());
        sock->readDatagram(data.data(), data.size(),&targetaddr,&targetport);
        if(!m_localIpList.contains(targetaddr.toString()))
        {
            QJsonParseError json_error;

            QJsonDocument jsonDoc(QJsonDocument::fromJson(decodedText(data), &json_error));
            if(json_error.error != QJsonParseError::NoError){
                qDebug() << "json error!" << json_error.errorString();
                return;
            }

            QJsonObject rootObj = jsonDoc.object();
            rootObj.insert("IP",targetaddr.toString());
            qDebug()<<"broadbast:"<<jsonDoc;
            // QJsonObject temp;
            // temp.insert("IP",targetaddr.toString());
            // QStringList getdata = decodedText(data).split("|");

            // for (int i=0;i<getdata.size();i++) {
            //     QStringList t = getdata[i].split(":");
            //     temp.insert(t.first(),t.last());
            // }

            emit getSocketData(rootObj);
        }
    }
}

bool netproperty::NetInterfaceIsUseful(int flags)
{
    //处于活动状态&&！环回接口&&支持组播
    if((flags & QNetworkInterface::IsUp)&&(!(flags & QNetworkInterface::IsLoopBack))&&(flags & QNetworkInterface::CanMulticast)){
        return true;
    }
    return false;
}

QByteArray netproperty::encodedText(QByteArray data) //加密
{
    return p_AES->encode(data, m_hashKey, m_hashIV);
}

QByteArray netproperty::decodedText(QByteArray data) //解密
{
    QByteArray decodeText = p_AES->decode(data, m_hashKey, m_hashIV);
    return p_AES->removePadding(decodeText);
}
/**
 * @brief        返回网卡类型说明
 * @param type   网卡类型枚举
 * @return       网卡类型说明
 */
QString netproperty::TypeToQString(int type)
{
    switch (type)
    {
    case QNetworkInterface::Loopback: return "虚拟环回接口，分配了环回 IP 地址 (127.0.0.1, ::1)";
    case QNetworkInterface::Virtual:  return "一种确定为虚拟的接口类型，但不是任何其他可能的类型";
    case QNetworkInterface::Ethernet: return "IEEE 802.3 以太网接口";
    case QNetworkInterface::Slip:     return "串行线路互联网协议接口";
    case QNetworkInterface::CanBus:   return "ISO 11898 控制器局域网总线接口";
    case QNetworkInterface::Ppp:      return "点对点协议接口，通过较低的传输层（通常通过无线电或物理线路串行）在两个节点之间建立直接连接";
    case QNetworkInterface::Fddi:     return "ANSI X3T12 光纤分布式数据接口，一种光纤局域网";
    case QNetworkInterface::Wifi:     return "IEEE 802.11 Wi-Fi 接口";         // 别名 Ieee80211
    case QNetworkInterface::Phonet:   return "使用 Linux Phonet socket系列的接口，用于与蜂窝调制解调器通信";
    case QNetworkInterface::Ieee802154: return "IEEE 802.15.4 个人区域网络接口，6LoWPAN 除外";
    case QNetworkInterface::SixLoWPAN:  return "6LoWPAN（低功耗无线个人区域网络上的 IPv6）接口，通常用于网状网络";
    case QNetworkInterface::Ieee80216:  return "IEEE 802.16 无线城域网";
    case QNetworkInterface::Ieee1394:   return "IEEE 1394 接口（又名“FireWire”）";
    case QNetworkInterface::Unknown:    return "接口类型无法确定或不是其他列出的类型之一";
    default:return "未知";
    }
}

/**
 * @brief        将网卡关联标志转换为可读的说明信息
 * @param flags
 * @return
 */
QString netproperty::FlagsToQString(int flags)
{
    QString strFlags;
    if(flags & QNetworkInterface::IsUp)
    {
        strFlags += "网络接口处于活动状态";
    }
    if(flags & QNetworkInterface::IsRunning)
    {
        strFlags.append(strFlags.isEmpty() ? "" : " | ");
        strFlags += "网络接口已分配资源";
    }
    if(flags & QNetworkInterface::CanBroadcast)
    {
        strFlags.append(strFlags.isEmpty() ? "" : " | ");
        strFlags += "网络接口工作在广播模式";
    }
    if(flags & QNetworkInterface::IsLoopBack)
    {
        strFlags.append(strFlags.isEmpty() ? "" : " | ");
        strFlags += "网络接口是一个环回接口";
    }
    if(flags & QNetworkInterface::IsPointToPoint)
    {
        strFlags.append(strFlags.isEmpty() ? "" : " | ");
        strFlags += "网络接口是一个点对点接口";
    }
    if(flags & QNetworkInterface::CanMulticast)
    {
        strFlags.append(strFlags.isEmpty() ? "" : " | ");
        strFlags += "网络接口支持组播";
    }
    return strFlags;
}
