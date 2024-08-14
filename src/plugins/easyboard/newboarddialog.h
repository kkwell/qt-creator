#pragma once

#include "easyboardstruct.h"

#include <QString>
#include <QDialog>

#include <utils/fancylineedit.h>

#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSpinBox>

// QT_BEGIN_NAMESPACE
// class QCheckBox;
// class QLineEdit;
// class QPushButton;
// QT_END_NAMESPACE

using namespace Utils;

namespace EasyBoard::Internal {

class NewBoardDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewBoardDialog(QWidget *parent = nullptr,Board *board = nullptr);

    QString getIp();
    QString getName();


private:
    void initializePage();
    bool isComplete() const;

    Board *m_board;
    QLabel *m_tip;
    FancyLineEdit *m_nameLineEdit;
    FancyLineEdit *m_hostNameLineEdit;
    QComboBox *m_typeBox;
    QSpinBox *m_sshPortSpinBox;

    // QPushButton *m_ok;
    // QPushButton *m_cancel;
};

} // namespace EasyBoard::Internal
