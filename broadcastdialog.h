#ifndef BROADCASTDIALOG_H
#define BROADCASTDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <vector>
#include <memory>
#include "feiqlib/feiqengine.h"

using namespace std;

class BroadcastDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BroadcastDialog(FeiqEngine* engine, QWidget *parent = nullptr);

private slots:
    void sendBroadcast();
    void selectAll();
    void selectNone();
    void updateSelectedCount();

private:
    void setupUI();
    void loadFellows();

private:
    FeiqEngine* mEngine;
    QListWidget* mFellowList;
    QTextEdit* mMessageEdit;
    QPushButton* mSendBtn;
    QLabel* mSelectedLabel;
    QLabel* mResultLabel;
};

#endif // BROADCASTDIALOG_H
