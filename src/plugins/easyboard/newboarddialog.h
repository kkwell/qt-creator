#pragma once

#include <QString>
#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

namespace EasyBoard::Internal {

class NewBoardDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewBoardDialog(QWidget *parent = nullptr);



private:

    QPushButton *m_openButton;
    QPushButton *m_renameButton;
    QPushButton *m_cloneButton;
    QPushButton *m_deleteButton;
    QCheckBox *m_autoLoadCheckBox;
};

} // namespace EasyBoard::Internal
