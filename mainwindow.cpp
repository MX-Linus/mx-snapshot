/**********************************************************************
 *  mainwindow.cpp
 **********************************************************************
 * Copyright (C) 2015 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of MX Snapshot.
 *
 * MX Snapshot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MX Snapshot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MX Snapshot.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include <QDebug>
#include <QFileDialog>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextStream>
#include <QTime>

#include "about.h"
#include "mainwindow.h"
#include "settings.h"
#include "ui_mainwindow.h"
#include "work.h"
#include <chrono>

using namespace std::chrono_literals;

MainWindow::MainWindow(QWidget *parent, const QCommandLineParser &arg_parser) :
    QDialog(parent),
    Settings(arg_parser),
    ui(new Ui::MainWindow),
    work(this)
{
    ui->setupUi(this);
    monthly = arg_parser.isSet(QStringLiteral("month"));
    setConnections();
    setup();
    loadSettings();
    listFreeSpace();
    setExclusions();
    setOtherOptions();

    if (monthly) {
        ui->btnNext->click();
        ui->btnNext->click();
    } else {
        listUsedSpace();
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

// load settings or use the default value
void MainWindow::loadSettings()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    ui->labelSnapshotDir->setText(snapshot_dir);
    if (snapshot_name.isEmpty())
        ui->lineEditName->setText(getFilename());
    else
        ui->lineEditName->setText(snapshot_name);
}

void MainWindow::setOtherOptions()
{
    ui->cbCompression->setCurrentIndex(ui->cbCompression->findText(compression));
    ui->checksums->setChecked(make_chksum);
    ui->radioRespin->setChecked(reset_accounts);
}

void MainWindow::setConnections()
{
    connect(qApp, &QApplication::aboutToQuit, [this] { cleanUp(); });
    connect(ui->btnAbout, &QPushButton::clicked, this, &MainWindow::btnAbout_clicked);
    connect(ui->btnBack, &QPushButton::clicked, this, &MainWindow::btnBack_clicked);
    connect(ui->btnCancel, &QPushButton::clicked, this, &MainWindow::btnCancel_clicked);
    connect(ui->btnEditExclude, &QPushButton::clicked, this, &MainWindow::btnEditExclude_clicked);
    connect(ui->btnHelp, &QPushButton::clicked, this, &MainWindow::btnHelp_clicked);
    connect(ui->btnNext, &QPushButton::clicked, this, &MainWindow::btnNext_clicked);
    connect(ui->btnSelectSnapshot, &QPushButton::clicked, this, &MainWindow::btnSelectSnapshot_clicked);
    connect(ui->cbCompression, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::cbCompression_currentIndexChanged);
    connect(ui->checksums, &QCheckBox::toggled, this, &MainWindow::checksums_toggled);
    connect(ui->excludeDesktop, &QCheckBox::toggled, this, &MainWindow::excludeDesktop_toggled);
    connect(ui->excludeDocuments, &QCheckBox::toggled, this, &MainWindow::excludeDocuments_toggled);
    connect(ui->excludeDownloads, &QCheckBox::toggled, this, &MainWindow::excludeDownloads_toggled);
    connect(ui->excludeMusic, &QCheckBox::toggled, this, &MainWindow::excludeMusic_toggled);
    connect(ui->excludeNetworks, &QCheckBox::toggled, this, &MainWindow::excludeNetworks_toggled);
    connect(ui->excludePictures, &QCheckBox::toggled, this, &MainWindow::excludePictures_toggled);
    connect(ui->excludeSteam, &QCheckBox::toggled, this, &MainWindow::excludeSteam_toggled);
    connect(ui->excludeVideos, &QCheckBox::toggled, this, &MainWindow::excludeVideos_toggled);
    connect(ui->excludeVirtualBox, &QCheckBox::toggled, this, &MainWindow::excludeVirtualBox_toggled);
    connect(ui->radioPersonal, &QRadioButton::clicked, this, &MainWindow::radioPersonal_clicked);
    connect(&timer, &QTimer::timeout, this, &MainWindow::progress);
    connect(shell, &Cmd::started, this, &MainWindow::procStart);
    connect(shell, &Cmd::finished, this, &MainWindow::procDone);
    connect(shell, &Cmd::outputAvailable, [](const QString &out) { qDebug().noquote() << out; });
    connect(shell, &Cmd::errorAvailable, [](const QString &out) { qWarning().noquote() << out; });
    connect(&work, &Work::message, this, &MainWindow::processMsg);
    connect(&work, &Work::messageBox, this, &MainWindow::processMsgBox);
}

void MainWindow::setExclusions()
{
    ui->excludeDesktop->setChecked(exclusions.testFlag(Exclude::Desktop));
    ui->excludeDocuments->setChecked(exclusions.testFlag(Exclude::Documents));
    ui->excludeDownloads->setChecked(exclusions.testFlag(Exclude::Downloads));
    ui->excludeMusic->setChecked(exclusions.testFlag(Exclude::Music));
    ui->excludeNetworks->setChecked(exclusions.testFlag(Exclude::Networks));
    ui->excludePictures->setChecked(exclusions.testFlag(Exclude::Pictures));
    ui->excludeSteam->setChecked(exclusions.testFlag(Exclude::Steam));
    ui->excludeVideos->setChecked(exclusions.testFlag(Exclude::Videos));
    ui->excludeVirtualBox->setChecked(exclusions.testFlag(Exclude::VirtualBox));
}

// setup/refresh versious items first time program runs
void MainWindow::setup()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    QFont font(QStringLiteral("monospace"));
    font.setStyleHint(QFont::Monospace);
    ui->outputBox->setFont(font);
    ui->outputBox->setReadOnly(true);

    this->setWindowTitle(tr("MX Snapshot"));
    ui->btnBack->setHidden(true);
    ui->stackedWidget->setCurrentIndex(0);
    ui->btnCancel->setEnabled(true);
    ui->btnNext->setEnabled(true);
    this->show();
}

// List used space
void MainWindow::listUsedSpace()
{
    this->show();
    ui->btnNext->setDisabled(true);
    ui->btnCancel->setDisabled(true);
    ui->btnSelectSnapshot->setDisabled(true);
    QString out = getUsedSpace();
    ui->btnNext->setEnabled(true);
    ui->btnCancel->setEnabled(true);
    ui->btnSelectSnapshot->setEnabled(true);
    ui->labelUsedSpace->setText(out);
}


// List free space on drives
void MainWindow::listFreeSpace()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    QString path = snapshot_dir;
    path.remove(QRegularExpression(QStringLiteral("/snapshot$")));
    QString free_space = getFreeSpaceStrings(path);
    ui->labelFreeSpace->clear();
    ui->labelFreeSpace->setText("- " + tr("Free space on %1, where snapshot folder is placed: ").arg(path) + free_space + "\n");
    ui->labelDiskSpaceHelp->setText(tr("The free space should be sufficient to hold the compressed data from / and /home\n\n"
                                       "      If necessary, you can create more available space\n"
                                       "      by removing previous snapshots and saved copies:\n"
                                       "      %1 snapshots are taking up %2 of disk space.\n").arg(QString::number(getSnapshotCount()), getSnapshotSize()));
}

// Installs package
bool MainWindow::installPackage(const QString &package)
{
    this->setWindowTitle(tr("Installing ") + package);
    ui->outputLabel->setText(tr("Installing ") + package);
    ui->outputBox->clear();
    ui->btnNext->setDisabled(true);
    ui->btnBack->setDisabled(true);
    ui->stackedWidget->setCurrentWidget(ui->outputPage);
    displayOutput();
    if (!work.installPackage(package)) {
        disableOutput();
        return false;
    }
    disableOutput();
    return true;
}

// clean up changes before exit
[[ noreturn ]] void MainWindow::cleanUp()
{
    ui->stackedWidget->setCurrentWidget(ui->outputPage);
    work.cleanUp();
}

void MainWindow::procStart()
{
    timer.start(500ms);
    setCursor(QCursor(Qt::BusyCursor));
}

void MainWindow::processMsgBox(BoxType box_type, const QString &title, const QString &msg)
{
    qDebug().noquote() << title << msg;
    switch (box_type)
    {
    case BoxType::warning: QMessageBox::warning(this, title, msg); break;
    case BoxType::critical: QMessageBox::critical(this, title, msg); break;
    case BoxType::question: QMessageBox::question(this, title, msg); break;
    case BoxType::information: QMessageBox::information(this, title, msg); break;
    }
}

void MainWindow::processMsg(const QString &msg)
{
    qDebug().noquote() << msg;
    ui->outputLabel->setText(msg);
}

void MainWindow::procDone()
{
    timer.stop();
    ui->progressBar->setValue(ui->progressBar->maximum());
    setCursor(QCursor(Qt::ArrowCursor));
}

void MainWindow::displayOutput()
{
    connect(shell, &Cmd::outputAvailable, this, &MainWindow::outputAvailable);
    connect(shell, &Cmd::errorAvailable, this, &MainWindow::outputAvailable);
}

void MainWindow::disableOutput()
{
    disconnect(shell, &Cmd::outputAvailable, this, &MainWindow::outputAvailable);
    disconnect(shell, &Cmd::errorAvailable, this, &MainWindow::outputAvailable);
}

// update output box
void MainWindow::outputAvailable(const QString &output)
{
    ui->outputBox->moveCursor(QTextCursor::End);
    if (output.contains(QLatin1String("\r"))) {
        ui->outputBox->moveCursor(QTextCursor::Up, QTextCursor::KeepAnchor);
        ui->outputBox->moveCursor(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    }
    ui->outputBox->insertPlainText(output);
    ui->outputBox->verticalScrollBar()->setValue(ui->outputBox->verticalScrollBar()->maximum());
}

void MainWindow::progress()
{
    ui->progressBar->setValue((ui->progressBar->value() + 1) % ui->progressBar->maximum());

    // in live environment and first page, blink text while calculating used disk space
    if (live && (ui->stackedWidget->currentIndex() == 0)) {
        if (ui->progressBar->value() % 4 == 0 )
            ui->labelUsedSpace->setText("\n " + tr("Please wait."));
        else
            ui->labelUsedSpace->setText("\n " + tr("Please wait. Calculating used disk space..."));
    }
}


// Next button clicked
void MainWindow::btnNext_clicked()
{
    QString file_name = ui->lineEditName->text();
    if (!file_name.endsWith(QLatin1String(".iso")))
        file_name += QLatin1String(".iso");

    if (QFile::exists(snapshot_dir + "/" + file_name)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Output file %1 already exists. Please use another file name, or delete the existent file.").arg(snapshot_dir + "/" + file_name));
        return;
    }

    // on first page
    if (ui->stackedWidget->currentIndex() == 0) {
        this->setWindowTitle(tr("Settings"));
        ui->stackedWidget->setCurrentWidget(ui->settingsPage);
        ui->btnBack->setHidden(false);
        ui->btnBack->setEnabled(true);
        selectKernel();
        ui->label_1->setText(tr("Snapshot will use the following settings:*"));

        ui->label_2->setText("\n" + tr("- Snapshot directory:") + " " + snapshot_dir + "\n" +
                       "- " + tr("Snapshot name:") + " " + file_name + "\n" +
                       tr("- Kernel to be used:") + " " + kernel + "\n");

    // on settings page
    } else if (ui->stackedWidget->currentWidget() == ui->settingsPage) {
        if (!checkCompression()) {
            processMsgBox(BoxType::critical, tr("Error"),
                tr("Current kernel doesn't support selected compression algorithm, please edit the configuration file and select a different algorithm."));
            return;
        }
        if (QMessageBox::Cancel == QMessageBox::question(this, tr("Final chance"),
                 tr("Snapshot now has all the information it needs to create an ISO from your running system.") + "\n\n" +
                 tr("It will take some time to finish, depending on the size of the installed system and the capacity of your computer.") + "\n\n" +
                 tr("OK to start?"), QMessageBox::Ok | QMessageBox::Cancel)) {
            return;
        }
        work.started = true;
        work.e_timer.start();
        if (!checkSnapshotDir()) {
            QMessageBox::critical(this, tr("Error"), tr("Could not create working directory. ") + snapshot_dir);
            cleanUp();
        }
        if (!checkTempDir()) {
            QMessageBox::critical(this, tr("Error"), tr("Could not create temporary directory. ") + snapshot_dir);
            cleanUp();
        }

        otherExclusions();
        ui->btnNext->setEnabled(false);
        ui->btnBack->setEnabled(false);
        ui->stackedWidget->setCurrentWidget(ui->outputPage);
        this->setWindowTitle(tr("Output"));
        ui->outputBox->clear();
        work.setupEnv();
        if (!monthly && !override_size)
            work.checkEnoughSpace();
        work.copyNewIso();
        ui->outputLabel->setText(QLatin1String(""));
        work.savePackageList(file_name);

        if (edit_boot_menu) {
            if (QMessageBox::Yes == QMessageBox::question(this, tr("Edit Boot Menu"),
                                  tr("The program will now pause to allow you to edit any files in the work directory. "
                                     "Select Yes to edit the boot menu or select No to bypass this step and continue creating the snapshot."),
                                     QMessageBox::Yes | QMessageBox::No)) {
                this->hide();
                QString cmd = getEditor() + " \"" + work_dir + "/iso-template/boot/isolinux/isolinux.cfg\"";
                shell->run(cmd);
                this->show();
            }
        }

        displayOutput();
        work.createIso(file_name);
        ui->btnCancel->setText(tr("Close"));
    } else {
        qApp->quit();
    }
}

void MainWindow::btnBack_clicked()
{
    this->setWindowTitle(tr("MX Snapshot"));
    ui->stackedWidget->setCurrentIndex(0);
    ui->btnNext->setEnabled(true);
    ui->btnBack->setHidden(true);
    ui->outputBox->clear();
}

void MainWindow::btnEditExclude_clicked()
{
    this->hide();
    shell->run(getEditor() + " " + snapshot_excludes.fileName());
    this->show();
}

void MainWindow::excludeDocuments_toggled(bool checked)
{
    excludeDocuments(checked);
    if (!checked) ui->excludeAll->setChecked(false);
}

void MainWindow::excludeDownloads_toggled(bool checked)
{
    excludeDownloads(checked);
    if (!checked) ui->excludeAll->setChecked(false);
}

void MainWindow::excludePictures_toggled(bool checked)
{
    excludePictures(checked);
    if (!checked) ui->excludeAll->setChecked(false);
}

void MainWindow::excludeMusic_toggled(bool checked)
{
    excludeMusic(checked);
    if (!checked) ui->excludeAll->setChecked(false);
}

void MainWindow::excludeVideos_toggled(bool checked)
{
    excludeVideos(checked);
    if (!checked) ui->excludeAll->setChecked(false);
}

void MainWindow::excludeDesktop_toggled(bool checked)
{
    excludeDesktop(checked);
    if (!checked) ui->excludeAll->setChecked(false);
}

void MainWindow::radioRespin_toggled(bool checked)
{
    reset_accounts = checked;
    if (checked && !ui->excludeAll->isChecked())
        ui->excludeAll->click();
}

void MainWindow::radioPersonal_clicked(bool checked)
{
    reset_accounts = !checked;
    if (checked && ui->excludeAll->isChecked())
        ui->excludeAll->click();
}


// About button clicked
void MainWindow::btnAbout_clicked()
{
    this->hide();
    displayAboutMsgBox(tr("About %1").arg(this->windowTitle()), "<p align=\"center\"><b><h2>" +
                       this->windowTitle() + "</h2></b></p><p align=\"center\">" +
                       tr("Version: ") + qApp->applicationVersion() + "</p><p align=\"center\"><h3>" +
                       tr("Program for creating a live-CD from the running system for MX Linux") +
                       R"(</h3></p><p align="center"><a href="http://mxlinux.org">http://mxlinux.org</a><br /></p><p align="center">)" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>",
                       QStringLiteral("/usr/share/doc/mx-snapshot/license.html"),
                       tr("%1 License").arg(this->windowTitle()), true);
    this->show();
}

// Help button clicked
void MainWindow::btnHelp_clicked()
{
    QLocale locale;
    QString lang = locale.bcp47Name();

    QString url = QStringLiteral("/usr/share/doc/mx-snapshot/mx-snapshot.html");

    if (lang.startsWith(QLatin1String("fr")))
        url = QStringLiteral("https://mxlinux.org/french-wiki/help-files-fr/help-mx-instantane");
    displayDoc(url, tr("%1 Help").arg(this->windowTitle()), true);
}

// Select snapshot directory
void MainWindow::btnSelectSnapshot_clicked()
{
    QFileDialog dialog;

    QString selected = QFileDialog::getExistingDirectory(this, tr("Select Snapshot Directory"), QString(), QFileDialog::ShowDirsOnly);
    if (!selected.isEmpty()) {
        snapshot_dir = selected + "/snapshot";
        ui->labelSnapshotDir->setText(snapshot_dir);
        listFreeSpace();
    }
}

// process keystrokes
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        closeApp();
}

// close application
void MainWindow::closeApp() {
    // ask for confirmation when on outputPage and not done
    if (ui->stackedWidget->currentWidget() == ui->outputPage && !work.done) {
        if (QMessageBox::Yes != QMessageBox::question(this, tr("Confirmation"),
                                                      tr("Are you sure you want to quit the application?"),
                                                      QMessageBox::Yes | QMessageBox::No))
            return;
    }
    cleanUp();
}

void MainWindow::btnCancel_clicked()
{
    closeApp();
}

void MainWindow::cbCompression_currentIndexChanged()
{
    QSettings settings(config_file.fileName(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("compression"), ui->cbCompression->currentText());
    compression = ui->cbCompression->currentText();
}

void MainWindow::excludeNetworks_toggled(bool checked)
{
    excludeNetworks(checked);
    if (!checked) ui->excludeAll->setChecked(false);
}

void MainWindow::checksums_toggled(bool checked)
{
    QSettings settings(config_file.fileName(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("make_md5sum"), checked ? QStringLiteral("yes") : QStringLiteral("no"));
    make_chksum = checked;
}

void MainWindow::excludeSteam_toggled(bool checked)
{
    excludeSteam(checked);
    if (!checked) ui->excludeAll->setChecked(false);
}

void MainWindow::excludeVirtualBox_toggled(bool checked)
{
    excludeVirtualBox(checked);
    if (!checked) ui->excludeAll->setChecked(false);
}
