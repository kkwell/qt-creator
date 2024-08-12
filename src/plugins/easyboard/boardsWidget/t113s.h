
#include <coreplugin/welcomepagehelper.h>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QMessageBox>
#include <QMovie>
#include <QPainter>
#include <QProgressDialog>
#include <QScrollArea>
#include <QSignalMapper>

using namespace Core;
using namespace Utils;
using namespace StyleHelper;
using namespace WelcomePageHelpers;

namespace EasyBoard::Internal {

class t113s : public QWidget
{
    static constexpr int dividerH = 16;

    Q_OBJECT

public:
    explicit t113s(QWidget *parent = nullptr);


    void update(const QModelIndex &current);

signals:


private:
    // QImage *m_img;
    QLabel *m_icon;
    QLabel *m_title;
    Button *m_vendor;
    QLabel *m_divider;
    // QWidget *m_dlCountItems;
    QLabel *m_details;
    QAbstractButton *installButton;
    QString m_currentVendor;
};

} // namespace EasyBoard::Internal
