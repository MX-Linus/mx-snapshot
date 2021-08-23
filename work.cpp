/**********************************************************************
 *  work.cpp
 **********************************************************************
 * Copyright (C) 2020 MX Authors
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
#include <QDate>
#include <QDirIterator>
#include <QRegularExpression>
#include <QSettings>

#include "work.h"

#define OUT settings->shell->getCmdOut
#define RUN settings->shell->run

Work::Work(Settings *settings) :
    settings(settings)
{
    if (!settings->checkCompression()) {
        emit messageBox(BoxType::critical, tr("Error"),
            tr("Current kernel doesn't support selected compression algorithm, please edit the configuration file and select a different algorithm."));
        cleanUp();
    }
}

Work::~Work()
{

}

// Checks if there's enough space on partitions, if not post error, cleanup and exit
// We don't yet take /home used space into considerations (need to calculate how much is excluded)
void Work::checkEnoughSpace()
{
    QStringList excludes;
    QFile *file = &settings->snapshot_excludes;

    // open and read the excludes file
    if (!file->open(QIODevice::ReadOnly))
        qDebug() << "Count not open file: " << file->fileName();
    while(!file->atEnd()) {
        QString line = file->readLine().trimmed();
        if (!line.startsWith("#") && !line.isEmpty() && !line.startsWith(".bind-root"))
            excludes << line.trimmed();
    }
    file->close();

    // add session excludes
    if (!settings->session_excludes.isEmpty()) {
        QString set = settings->session_excludes;
        set.remove(0, 3); // remove "-e "
        for (QString tmp : set.split("\" \"")) {
            tmp.remove("\"");
            tmp.remove(0, 0);
            excludes << tmp;
        }
    }

    QString root_dev = OUT("df /.bind-root --output=target |tail -1", true);
    QMutableStringListIterator it(excludes);
    while (it.hasNext()) {
        it.next();
        if (it.value().indexOf("!") != -1) // remove things like "!(minstall.desktop)"
            it.value().truncate(it.value().indexOf("!"));
        it.value().replace(" ", "\\ "); // escape special bash characters, might need to expand this
        it.value().replace("(", "\\(");
        it.value().replace(")", "\\)");
        it.value().replace("|", "\\|");
        it.value().prepend("/.bind-root/"); // check size occupied by excluded files on /.bind-root only
        it.value().remove(QRegularExpression("\\*$")); // chop last *
        // remove from list if files not on the same volume
        if (root_dev != OUT("df " + it.value() + " --output=target 2>/dev/null |tail -1", true))
            it.remove();
    }
    emit message(tr("Calculating total size of excluded files..."));
    bool ok;
    QString cmd = settings->live ? "du -sc" : "du -sxc";
    quint64 excl_size = OUT(cmd + " {" + excludes.join(",").remove("/.bind-root,")
                                                       + "} 2>/dev/null |tail -1 |cut -f1").toULongLong(&ok);
    if (!ok) {
        qDebug() << "Error: calculating size of excluded files\n"\
                    "If you are sure you have enough free space rerun the program with -o/--override-size option";
        cleanUp();
    }
    emit message(tr("Calculating size of root..."));
    cmd = settings->live ? "du -s" : "du -sx";
    quint64 root_size = OUT(cmd + " /.bind-root 2>/dev/null |tail -1 |cut -f1").toULongLong(&ok);
    if (!ok) {
        qDebug() << "Error: calculating root size (/.bind-root)\n"\
                    "If you are sure you have enough free space rerun the program with -o/--override-size option";
        cleanUp();
    }
    qDebug() << "SIZE ROOT" << root_size;
    qDebug() << "SIZE EXCL" << excl_size;
    qDebug() << "SIZE FREE" << settings->free_space;
    if (excl_size > root_size) {
        qDebug() << "Error: calculating excluded file size.\n"\
                    "If you are sure you have enough free space rerun the program with -o/--overrde-size option";
        cleanUp();
    }

    uint c_factor = compression_factor.value(settings->compression);
    quint64 adjusted_root = (root_size - excl_size) * c_factor / 100;
    // if snapshot and workdir are on the same partition we need about double the size of adjusted_root
    if (settings->shell->getCmdOut("stat -c '%d' " + settings->work_dir) ==
            settings->shell->getCmdOut("stat -c '%d' " + settings->snapshot_dir)) {
        if (settings->free_space < adjusted_root * 2) {
            emit messageBox(BoxType::critical, tr("Error"),
                            tr("There's not enough free space on your target disk, you need at least %1").arg(QString::number(adjusted_root * 2 / 1048576.0, 'f', 2) + "GiB" ) + "\n" +
                            tr("You have %1 free space on %2").arg(QString::number(settings->free_space / 1048576.0, 'f', 2) + "GiB").arg(settings->snapshot_dir));
            cleanUp();
        }
    } else { // if not on the same partitions, check if each work_dir and snapshot_dir partitions have enough free space for 1 adjusted_root
        if (settings->free_space_work < adjusted_root) {
            emit messageBox(BoxType::critical, tr("Error"),
                            tr("There's not enough free space on your target disk, you need at least %1").arg(QString::number(adjusted_root / 1048576.0, 'f', 2) + "GiB") + "\n" +
                            tr("You have %1 free space on %2").arg(QString::number(settings->free_space_work / 1048576.0, 'f', 2) + "GiB").arg(settings->work_dir));
            cleanUp();
        }
        if (settings->free_space < adjusted_root) {
            emit messageBox(BoxType::critical, tr("Error"),
                            tr("There's not enough free space on your target disk, you need at least %1").arg(QString::number(adjusted_root / 1048576.0, 'f', 2) + "GiB") + "\n" +
                            tr("You have %1 free space on %2").arg(QString::number(settings->free_space / 1048576.0, 'f', 2) + "GiB").arg(settings->snapshot_dir));
            cleanUp();
        }
    }
}

// Checks if package is installed
bool Work::checkInstalled(const QString &package)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    if (OUT(QString("dpkg -s %1 |grep Status").arg(package)) == "Status: install ok installed")
        return true;
    return false;
}

// Clean up changes before exit
void Work::cleanUp()
{
    if (!started ) {
        settings->shell->halt();
        initrd_dir.remove();
        exit(EXIT_SUCCESS);
    }
    settings->shell->halt();
    emit message(tr("Cleaning..."));
    system("sync");

    system("pkill mksquashfs; pkill md5sum");
    QDir::setCurrent("/");
    if (QFileInfo::exists("/tmp/installed-to-live/cleanup.conf"))
        system("installed-to-live cleanup");

    if (!settings->live && !settings->reset_accounts)
        system("rm /home/*/Desktop/minstall.desktop 2>/dev/null");

    if (!settings->live) QFile::remove("/etc/skel/Desktop/Installer.desktop");

    initrd_dir.remove();
    settings->tmpdir.reset();
    if (done) {
        qDebug().noquote() << tr("Done");
        exit(EXIT_SUCCESS);
    } else {
        qDebug().noquote() << tr("Done") << "\n";
        qDebug().noquote() << QObject::tr("Interrupted or failed to complete");
        exit(EXIT_FAILURE);
    }
}

// Check if we can put work_dir on another partition with enough space, move work_dir there and setupEnv again
bool Work::checkAndMoveWorkDir(const QString &dir, quint64 req_size)
{
    // see first if the dir is on different partition otherwise it's irrelevant
    if (OUT("stat -c '%d' " + dir) != OUT("stat -c '%d' " + settings->snapshot_dir)
            && settings->getFreeSpace(dir) > req_size) {
        if (QFileInfo::exists("/tmp/installed-to-live/cleanup.conf"))
            RUN("installed-to-live cleanup");
        settings->tempdir_parent = dir;
        if (!settings->checkTempDir())
            cleanUp();
        setupEnv();
        return true;
    }
    return false;
}

void Work::checkNoSpaceAndExit(quint64 needed_space, quint64 free_space, const QString &dir)
{
    if (needed_space > free_space) {
        emit messageBox(BoxType::critical, tr("Error"),
                    tr("There's not enough free space on your target disk, you need at least %1").arg(QString::number(needed_space / 1048576.0, 'f', 2) + "GiB") + "\n" +
                    tr("You have %1 free space on %2").arg(QString::number(free_space / 1048576.0, 'f', 2) + "GiB").arg(dir) + "\n" +
                    tr("If you are sure you have enough free space rerun the program with -o/--override-size option"));
        cleanUp();
    }
}

void Work::closeInitrd(const QString &initrd_dir, const QString &file)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    QDir::setCurrent(initrd_dir);
    RUN("(find . |cpio -o -H newc --owner root:root |gzip -9) >\"" + file + "\"");
    makeChecksum(HashType::md5, settings->work_dir + "/iso-template/antiX", "initrd.gz");
}


// copyModules(mod_dir/kernel kernel)
void Work::copyModules(const QString &to, const QString &kernel)
{
    QString kernel586 = "3.16.0-4-586";
    QString cmd = QString("/usr/share/mx-packageinstaller/scripts/copy-initrd-modules -t=\"%1\" -k=\"%2\"").arg(to).arg(kernel);
    RUN(cmd);
    // copy 586 modules for the non-PAE kernel
    if (settings->i686 && settings->debian_version < 9) {  // Not applicable for Stretch (MX17) or more
        QString cmd = QString("/usr/share/mx-packageinstaller/scripts/copy-initrd-modules -t=\"%1\" -k=\"%2\"").arg(to).arg(kernel586);
        RUN(cmd);
    }
    cmd = QString("/usr/share/mx-packageinstaller/scripts/copy-initrd-programs --to=\"%1\"").arg(to);
    RUN(cmd);
}


// Copying the iso-template filesystem
void Work::copyNewIso()
{
    emit message(tr("Copying the new-iso filesystem..."));
    QDir::setCurrent(settings->work_dir);

    RUN("tar xf /usr/lib/iso-template/iso-template.tar.gz");

    RUN("cp /usr/lib/iso-template/template-initrd.gz iso-template/antiX/initrd.gz");

    RUN("cp /boot/vmlinuz-" + settings->kernel + " iso-template/antiX/vmlinuz");

    if (settings->debian_version < 9) { // Only for versions older than Stretch
        if (settings->i686) {
            RUN("cp /boot/vmlinuz-3.16.0-4-586 iso-template/antiX/vmlinuz1");
        } else {
            // mv x64 template files over
            RUN("mv iso-template/boot/grub/grub.cfg_x64 iso-template/boot/grub/grub.cfg");
            RUN("mv iso-template/boot/syslinux/syslinux.cfg_x64 iso-template/boot/syslinux/syslinux.cfg");
            RUN("mv iso-template/boot/isolinux/isolinux.cfg_x64 iso-template/boot/isolinux/isolinux.cfg");
        }
    }

    replaceMenuStrings();
    makeChecksum(HashType::md5, settings->work_dir + "/iso-template/antiX", "vmlinuz");

    QString path = initrd_dir.path();
    if (!initrd_dir.isValid()) {
        qDebug() << tr("Could not create temp directory. ") + path;
        cleanUp();
    }

    openInitrd(settings->work_dir + "/iso-template/antiX/initrd.gz", path);

    // Strip modules; make sure initrd_dir is correct to avoid disaster
    if (path.startsWith("/tmp/") && QFileInfo::exists(path + "/lib/modules"))
         RUN("rm -r \"" + path  + "/lib/modules\"");

    // We cannot count on this file in the future versions
    RUN("test -r /usr/local/share/live-files/files/etc/initrd-release && cp /usr/local/share/live-files/files/etc/initrd-release \"" + path + "/etc\"");

    // Overwrite with this file, probably a better location _if_ the file exists
    RUN("test -r /etc/initrd-release && cp /etc/initrd-release \"" + path + "/etc\"");
    if (initrd_dir.isValid()) {
        copyModules(path, settings->kernel);
        closeInitrd(path, settings->work_dir + "/iso-template/antiX/initrd.gz");
        initrd_dir.remove();
    }
}

// Create squashfs and then the iso
bool Work::createIso(const QString &filename)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    // squash the filesystem copy
    QDir::setCurrent(settings->work_dir);
    QString cmd;
    cmd = "mksquashfs /.bind-root iso-template/antiX/linuxfs -comp " + settings->compression
            + ((settings->mksq_opt.isEmpty()) ? "" : " " + settings->mksq_opt)
            + " -wildcards -ef " + settings->snapshot_excludes.fileName() + " " + settings->session_excludes;

    emit message(tr("Squashing filesystem..."));
    if (!RUN(cmd)) {
        emit messageBox(BoxType::critical, tr("Error"),
                        tr("Could not create linuxfs file, please check whether you have enough space on the destination partition."));
        return false;
    }

    // mv linuxfs to another folder
    QDir().mkpath("iso-2/antiX");
    RUN("mv iso-template/antiX/linuxfs* iso-2/antiX");
    makeChecksum(HashType::md5, settings->work_dir + "/iso-2/antiX", "linuxfs");

    RUN("installed-to-live cleanup");

    // create the iso file
    QDir::setCurrent(settings->work_dir + "/iso-template");
    cmd = "xorriso -as mkisofs -l -V MXLIVE -R -J -pad -iso-level 3 -no-emul-boot -boot-load-size 4 -boot-info-table -b boot/isolinux/isolinux.bin  -eltorito-alt-boot -e boot/grub/efi.img -no-emul-boot -c boot/isolinux/isolinux.cat -o \"" +
            settings->snapshot_dir + "/" + filename + "\" . \""  + settings->work_dir + "/iso-2\"";
    emit message(tr("Creating CD/DVD image file..."));
    if (!RUN(cmd)) {
        emit messageBox(BoxType::critical, tr("Error"), tr("Could not create ISO file, please check whether you have enough space on the destination partition."));
        return false;
    }
    system("chown $(logname):$(logname) \"" + settings->snapshot_dir.toUtf8() + "/" + filename.toUtf8( )+ "\"");

    // make it isohybrid
    if (settings->make_isohybrid) {
        emit message(tr("Making hybrid iso"));
        cmd = "isohybrid --uefi \"" + settings->snapshot_dir + "/" + filename + "\"";
        RUN(cmd);
    }

    // make ISO checksums
    if (settings->make_chksum) {
        makeChecksum(HashType::md5, settings->snapshot_dir, filename);
        makeChecksum(HashType::sha512, settings->snapshot_dir, filename);
        system("chown $(logname):$(logname) \"" + settings->snapshot_dir.toUtf8() + "/" + filename.toUtf8() + ".md5\"");
        system("chown $(logname):$(logname) \"" + settings->snapshot_dir.toUtf8() + "/" + filename.toUtf8() + ".sha512\"");
    }

    QTime time(0, 0);
    time = time.addMSecs(static_cast<int>(e_timer.elapsed()));
    emit message(tr("Done"));
    emit messageBox(BoxType::information, tr("Success"), tr("MX Snapshot completed sucessfully!") + "\n" +
                    tr("Snapshot took %1 to finish.").arg(time.toString("hh:mm:ss")) + "\n\n" +
                    tr("Thanks for using MX Snapshot, run MX Live USB Maker next!"));
    done = true;
    return true;
}


// Installs package
bool Work::installPackage(const QString &package)
{
    emit message(tr("Installing ") + package);
    RUN("apt-get update");
    if (!RUN("apt-get install -y " + package)) {
        emit messageBox(BoxType::critical, tr("Error"), tr("Could not install ") + package);
        return false;
    }
    return true;
}

// Create checksums for different files
void Work::makeChecksum(const HashType &hash_type, const QString &folder, const QString &file_name)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    emit message(tr("Calculating checksum..."));
    RUN("sync");
    QDir::setCurrent(folder);
    QString ce = QVariant::fromValue(hash_type).toString();
    QString cmd;
    QString checksum_cmd =  QString("%1sum \"" + file_name + "\">\"" + folder + "/" + file_name + ".%1\"").arg(ce);
    QString temp_dir =  "/tmp/snapsphot-checksum-temp";
    QString checksum_tmp =  QString("TD=" + temp_dir + "; KEEP=$TD/.keep; [ -d $TD ] || mkdir $TD ; FN=\"" + file_name + "\"; CF=\""
            + folder + "/${FN}.%1\"; cp $FN $TD/$FN; pushd $TD>/dev/null; %1sum $FN > $FN.%1 ; cp $FN.%1 $CF; popd >/dev/null ; [ -e $KEEP ] || rm -rf $TD").arg(ce);

    if (settings->preempt) {
        // check free space available on /tmp
        RUN("TF=/tmp/snapsphot-checksum-temp/\"" + file_name + "\"; [ -f \"$TF\" ] && rm -f \"$TF\"");
        if (!RUN("DUF=$(du -BM " + file_name
                                     + "|grep -oE '^[[:digit:]]+'); TDA=$(df -BM --output=avail /tmp |grep -oE '^[[:digit:]]+'); ((TDA/10*8 >= DUF))"))
            settings->preempt = false;
    }
    if (!settings->preempt) {
        cmd = checksum_cmd;
    } else {
        // free pagecache
        RUN("sync; sleep 1; echo 1 > /proc/sys/vm/drop_caches; sleep 1");
        cmd = checksum_tmp;
    }
    RUN(cmd);
    QDir::setCurrent(settings->work_dir);
}

void Work::openInitrd(const QString &file, const QString &initrd_dir)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    emit message(tr("Building new initrd..."));
    RUN("chmod a+rx \"" + initrd_dir + "\"");
    QDir::setCurrent(initrd_dir);
    RUN(QString("gunzip -c \"%1\" |cpio -idum").arg(file));
}

// Replace text in menu items in grub.cfg, syslinux.cfg, isolinux.cfg
void Work::replaceMenuStrings() {
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";

    QString distro, full_distro_name, full_distro_name_space;
    QString distro_version_file = "";
    if (QFileInfo::exists("/etc/antix-version"))
        distro_version_file = "/etc/antix-version";
    else if (QFileInfo::exists("/etc/mx-version"))
        distro_version_file = "/etc/mx-version";

    if (distro_version_file.length() > 0) {
        distro = OUT("cut -f1 -d'_' " + distro_version_file);
        full_distro_name = OUT("cut -f1 -d' ' " + distro_version_file);
    } else {
        distro = "MX_" + QString(settings->i686 ? "386" : "x64");
        full_distro_name = distro;
    }

    full_distro_name_space = full_distro_name;
    full_distro_name_space.replace("_", " ");

    QString date = QDate::currentDate().toString("dd MMMM yyyy");
    if (!QFileInfo::exists("/etc/lsb-release")) {
        emit messageBox(BoxType::critical, tr("Error"), tr("Could not find %1 file, cannot continue").arg("/etc/lsb-release"));
        cleanUp();
    }

    QString distro_name = OUT("grep -oP '(?<=DISTRIB_ID=).*' /etc/lsb-release");
    distro_name.replace("\"", "");
    QString code_name = OUT("grep -oP '(?<=DISTRIB_CODENAME=).*' /etc/lsb-release");
    code_name.replace("\"", "");

    QString options = "quiet";

    if (settings->debian_version < 9) { // Only for versions older than Stretch which uses old mx-iso-template
        if (settings->i686) {
            QString new_string = "MX Linux 386 (" + date + ")";
            replaceStringInFile("custom-name", new_string, settings->work_dir + "/iso-template/boot/grub/grub.cfg");
            replaceStringInFile("custom-name", new_string, settings->work_dir + "/iso-template/boot/syslinux/syslinux.cfg");
            replaceStringInFile("custom-name", new_string, settings->work_dir + "/iso-template/boot/isolinux/isolinux.cfg");
        } else {
            QString new_string = "MX Linux x64 (" + date + ")";
            replaceStringInFile("custom-name", new_string, settings->work_dir + "/iso-template/boot/grub/grub.cfg");
            replaceStringInFile("custom-name", new_string, settings->work_dir + "/iso-template/boot/syslinux/syslinux.cfg");
            replaceStringInFile("custom-name", new_string, settings->work_dir + "/iso-template/boot/isolinux/isolinux.cfg");
        }

    } else { // with new mx-iso-template for MX-17 and greater
        replaceStringInFile("%DISTRO%", distro, settings->work_dir + "/iso-template/boot/grub/grub.cfg");
        replaceStringInFile("%DISTRO_NAME%", distro_name, settings->work_dir + "/iso-template/boot/grub/grub.cfg");
        replaceStringInFile("%FULL_DISTRO_NAME%", full_distro_name, settings->work_dir + "/iso-template/boot/grub/grub.cfg");
        replaceStringInFile("%FULL_DISTRO_NAME_SPACE%", full_distro_name_space, settings->work_dir + "/iso-template/boot/grub/grub.cfg");
        replaceStringInFile("%OPTIONS%", options, settings->work_dir + "/iso-template/boot/grub/grub.cfg");
        replaceStringInFile("%RELEASE_DATE%", date, settings->work_dir + "/iso-template/boot/grub/grub.cfg");

        replaceStringInFile("%OPTIONS%", options, settings->work_dir + "/iso-template/boot/syslinux/syslinux.cfg");
        replaceStringInFile("%OPTIONS%", options, settings->work_dir + "/iso-template/boot/isolinux/isolinux.cfg");

        replaceStringInFile("%FULL_DISTRO_NAME%", full_distro_name, settings->work_dir + "/iso-template/boot/syslinux/syslinux.cfg");
        replaceStringInFile("%FULL_DISTRO_NAME%", full_distro_name, settings->work_dir + "/iso-template/boot/syslinux/readme.msg");
        replaceStringInFile("%FULL_DISTRO_NAME%", full_distro_name, settings->work_dir + "/iso-template/boot/isolinux/isolinux.cfg");
        replaceStringInFile("%FULL_DISTRO_NAME%", full_distro_name, settings->work_dir + "/iso-template/boot/isolinux/readme.msg");

        replaceStringInFile("%RELEASE_DATE%", date, settings->work_dir + "/iso-template/boot/syslinux/syslinux.cfg");
        replaceStringInFile("%RELEASE_DATE%", date, settings->work_dir + "/iso-template/boot/syslinux/readme.msg");
        replaceStringInFile("%RELEASE_DATE%", date, settings->work_dir + "/iso-template/boot/isolinux/isolinux.cfg");
        replaceStringInFile("%RELEASE_DATE%", date, settings->work_dir + "/iso-template/boot/isolinux/readme.msg");

        replaceStringInFile("%CODE_NAME%", code_name, settings->work_dir + "/iso-template/boot/syslinux/syslinux.cfg");
        replaceStringInFile("%CODE_NAME%", code_name, settings->work_dir + "/iso-template/boot/isolinux/isolinux.cfg");

        QString themeDir = settings->work_dir + "/iso-template/boot/grub/theme";
        QDirIterator themeFileIt(themeDir, {"*.txt"}, QDir::Files);
        QString themeFile;
        while (themeFileIt.hasNext()) {
            themeFile = themeFileIt.next();
            replaceStringInFile("%ASCII_CODE_NAME%", code_name, themeFile);
            replaceStringInFile("%DISTRO%", distro, themeFile);
        }
    }
}

// Util function for replacing strings in files
bool Work::replaceStringInFile(const QString &old_text, const QString &new_text, const QString &file_path)
{
    return RUN(QString("sed -i 's/%1/%2/g' \"%3\"").arg(old_text).arg(new_text).arg(file_path));
}

// Save package list in working directory
void Work::savePackageList(const QString &file_name)
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    QFileInfo fi(file_name);
    QDir dir(settings->work_dir + "/iso-template/" + fi.completeBaseName());
    if (!dir.mkpath(dir.absolutePath()))
        emit messageBox(BoxType::critical, tr("Error"), tr("Could not create working directory. ") + dir.absolutePath());
    QString full_name = settings->work_dir + "/iso-template/" + fi.completeBaseName() + "/package_list";
    QString cmd = "dpkg -l |grep ^ii\\ \\ |awk '{print $2,$3}' |sed 's/:'$(dpkg --print-architecture)'//' |column -t >\""
            + full_name + "\"";
    RUN(cmd);
}

// Setup the environment before taking the snapshot
void Work::setupEnv()
{
    qDebug() << "+++" << __PRETTY_FUNCTION__ << "+++";
    // checks if work_dir looks OK
    if (!settings->work_dir.contains("/mx-snapshot"))
        cleanUp();

    QString bind_boot = "";
    QString bind_boot_too = "";
    if (RUN("mountpoint /boot")) {
        bind_boot = "bind=/boot ";
        bind_boot_too = ",/boot";
    }

    // install mx-installer if absent
    if (settings->force_installer && !checkInstalled("mx-installer"))
        installPackage("mx-installer");

    writeSnapshotInfo();

    // setup environment if creating a respin (reset root/demo, remove personal accounts)
    if (settings->reset_accounts) {
        RUN("installed-to-live -b /.bind-root start " + bind_boot + "empty=/home general version-file read-only");
    } else {
        if (settings->force_installer) {  // copy minstall.desktop to Desktop on all accounts
            RUN("echo /home/*/Desktop |xargs -n1 cp /usr/share/applications/minstall.desktop 2>/dev/null");
            // Needs write access to remove lock symbol on installer on desktop, executable to run it
            RUN("chmod 777 /home/*/Desktop/minstall.desktop");
            if (!QFile::exists("xdg-user-dirs-update.real")) {
                QDir().mkdir("/etc/skel/Desktop");
                QFile::copy("/usr/share/applications/minstall.desktop", "/etc/skel/Desktop/Installer.desktop");
                RUN("chmod 755 /etc/skel/Desktop/Installer.desktop");
            }
        }
        RUN("installed-to-live -b /.bind-root start bind=/home" + bind_boot_too + " live-files version-file adjtime read-only");
    }
}

// Write date of the snapshot in a "snapshot_created" file
void Work::writeSnapshotInfo()
{
    QFile file("/usr/local/share/live-files/files/etc/snapshot_created");
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
        return;

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString("yyyyMMdd_HHmm");
    file.close();
}
