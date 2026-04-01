#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
#include <QTabWidget>
#include <QPushButton>
#include <QLabel>
#include "settings.h"

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(Settings* settings, QWidget *parent = nullptr);

private slots:
    void onSave();
    void onCancel();

private:
    void setupUI();
    void loadSettings();
    void saveSettings();

private:
    Settings* mSettings;

    // 用户设置
    QLineEdit* mUserNameEdit;
    QLineEdit* mHostNameEdit;

    // 网络设置
    QTextEdit* mCustomGroupEdit;

    // 发送方式
    QRadioButton* mSendByEnter;
    QRadioButton* mSendByCtrlEnter;

    // 通知设置
    QCheckBox* mEnableNotify;

    // 自动应答
    QCheckBox* mEnableAutoReply;
    QTextEdit* mAutoReplyText;

    // 按钮
    QPushButton* mSaveBtn;
    QPushButton* mCancelBtn;
};

#endif // SETTINGSDIALOG_H
