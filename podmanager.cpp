///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//    This file is part of qt-pods.                                          //
//    Copyright (C) 2015 Jacob Dawid, jacob@omg-it.works                     //
//                                                                           //
//    qt-pods is free software: you can redistribute it and/or modify        //
//    it under the terms of the GNU General Public License as published by   //
//    the Free Software Foundation, either version 3 of the License, or      //
//    (at your option) any later version.                                    //
//                                                                           //
//    qt-pods is distributed in the hope that it will be useful,             //
//    but WITHOUT ANY WARRANTY; without even the implied warranty of         //
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          //
//    GNU General Public License for more details.                           //
//                                                                           //
//    You should have received a copy of the GNU General Public License      //
//    along with qt-pods. If not, see <http://www.gnu.org/licenses/>.        //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

// Own includes
#include "podmanager.h"

// Qt includes
#include <QDir>
#include <QSettings>
#include <QProcess>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>

PodManager::PodManager(QObject *parent)
    : QObject(parent) {
    qRegisterMetaType<QList<Pod> >("QList<Pod>");
    _networkAccessManager = new QNetworkAccessManager(this);
}

bool PodManager::isGitRepository(QString repository) {
    QDir dir(repository);
    QString gitPath = dir.filePath(".git");
    bool result = QFile::exists(gitPath);
    emit isGitRepositoryFinished(repository, result);
    return result;
}

bool PodManager::installPod(QString repository, Pod pod) {
    if(!isGitRepository(repository)) {
        emit installPodFinished(repository, pod, false);
        return false;
    }

    QDir cwd = QDir::current();
    QDir::setCurrent(repository);

    int result = QProcess::execute(QString("git submodule add %1 %2").arg(pod.url).arg(pod.name));

    QDir::setCurrent(cwd.absolutePath());

    if(result != 0) {
        emit installPodFinished(repository, pod, false);
        return false;
    }

    // Try to store meta data in .gitmodules
    writePodInfo(repository, pod);
    generatePodsPri(repository);
    generatePodsSubdirsPri(repository);
    generateSubdirsPro(repository);

    emit installPodFinished(repository, pod, true);
    return true;
}

bool PodManager::installPods(QString repository, QList<Pod> pods) {
    if(!isGitRepository(repository)) {
        emit installPodsFinished(repository, pods, false);
        return false;
    }

    QDir cwd = QDir::current();
    QDir::setCurrent(repository);

    bool success = true;
    foreach(Pod pod, pods) {
        int result = QProcess::execute(QString("git submodule add %1 %2").arg(pod.url).arg(pod.name));
        if(result == 0) {
            writePodInfo(repository, pod);
        }
        success = success && (result == 0);
    }

    QDir::setCurrent(cwd.absolutePath());

    if(!success) {
        emit installPodsFinished(repository, pods, false);
        return false;
    }

    generatePodsPri(repository);
    generatePodsSubdirsPri(repository);
    generateSubdirsPro(repository);

    emit installPodsFinished(repository, pods, true);
    return true;
}

bool PodManager::removePod(QString repository, QString podName) {
    if(!isGitRepository(repository)) {
        emit removePodFinished(repository, podName, false);
        return false;
    }

    QDir cwd = QDir::current();
    QDir::setCurrent(repository);

    int deinitResult = -1, remove1Result = -1, remove2Result = -1;
    deinitResult = QProcess::execute(QString("git submodule deinit -f %1").arg(podName));
    if(deinitResult == 0) {
        remove1Result = QProcess::execute(QString("git rm -rf %1").arg(podName));
        if(remove1Result == 0) {
            remove2Result = QProcess::execute(QString("rm -rf %1/.git/modules/%2").arg(repository).arg(podName));
        }
    }

    QDir::setCurrent(cwd.absolutePath());

    if((deinitResult != 0) && (remove1Result != 0) && (remove2Result != 0)) {
        emit removePodFinished(repository, podName, false);
        return false;
    }

    purgePodInfo(repository, podName);
    generatePodsPri(repository);
    generatePodsSubdirsPri(repository);
    generateSubdirsPro(repository);

    emit removePodFinished(repository, podName, true);
    return true;
}

bool PodManager::removePods(QString repository, QStringList podNames) {
    if(!isGitRepository(repository)) {
        emit removePodsFinished(repository, podNames, false);
        return false;
    }

    QDir cwd = QDir::current();
    QDir::setCurrent(repository);

    bool success = true;
    foreach(QString podName, podNames) {
        int deinitResult = -1, remove1Result = -1, remove2Result = -1;
        deinitResult = QProcess::execute(QString("git submodule deinit -f %1").arg(podName));
        if(deinitResult == 0) {
            remove1Result = QProcess::execute(QString("git rm -rf %1").arg(podName));
            if(remove1Result == 0) {
                remove2Result = QProcess::execute(QString("rm -rf %1/.git/modules/%2").arg(repository).arg(podName));
                purgePodInfo(repository, podName);
            }
        }
        success = success && ((deinitResult == 0) && (remove1Result == 0) && (remove2Result == 0));
    }

    QDir::setCurrent(cwd.absolutePath());

    if(!success) {
        emit removePodsFinished(repository, podNames, false);
        return false;
    }

    generatePodsPri(repository);
    generatePodsSubdirsPri(repository);
    generateSubdirsPro(repository);

    emit removePodsFinished(repository, podNames, true);
    return true;
}

bool PodManager::updatePod(QString repository, QString podName) {
    if(!isGitRepository(repository)) {
        emit updatePodFinished(repository, podName, false);
        return false;
    }

    QDir cwd = QDir::current();
    QDir::setCurrent(QDir(repository).absoluteFilePath(podName));

    int stashResult = 1, checkoutResult = 1, pullResult = 1;
    stashResult = QProcess::execute(QString("git stash"));
    if(stashResult == 0) {
        checkoutResult = QProcess::execute(QString("git checkout master"));
        if(checkoutResult == 0) {
            pullResult = QProcess::execute(QString("git pull"));
        }
    }

    QDir::setCurrent(cwd.absolutePath());
    bool result = (stashResult == 0) && (checkoutResult == 0) && (pullResult == 0);
    emit updatePodFinished(repository, podName, result);
    return result;
}

bool PodManager::updatePods(QString repository, QStringList podNames) {
    if(!isGitRepository(repository)) {
        emit updatePodsFinished(repository, podNames, false);
        return false;
    }

    QDir cwd = QDir::current();

    bool success = true;
    foreach(QString podName, podNames) {
        QDir::setCurrent(QDir(repository).absoluteFilePath(podName));

        int stashResult = 1, checkoutResult = 1, pullResult = 1;
        stashResult = QProcess::execute(QString("git stash"));
        if(stashResult == 0) {
            checkoutResult = QProcess::execute(QString("git checkout master"));
            if(checkoutResult == 0) {
                pullResult = QProcess::execute(QString("git pull"));
            }
        }
        success = success && ((stashResult == 0) && (checkoutResult == 0) && (pullResult == 0));
    }

    QDir::setCurrent(cwd.absolutePath());

    if(!success) {
        emit updatePodsFinished(repository, podNames, false);
    }
    emit updatePodsFinished(repository, podNames, true);
    return true;
}

bool PodManager::updateAllPods(QString repository) {
    if(!isGitRepository(repository)) {
        emit updateAllPodsFinished(repository, false);
        return false;
    }

    bool result = false;
    QList<Pod> pods = installedPods(repository);
    foreach(Pod pod, pods) {
        result = result && updatePod(repository, pod.name);
    }

    if(!result) {
        emit updateAllPodsFinished(repository, false);
        return false;
    }

    generatePodsPri(repository);
    generatePodsSubdirsPri(repository);
    generateSubdirsPro(repository);

    emit updateAllPodsFinished(repository, true);
    return true;
}

QList<Pod> PodManager::installedPods(QString repository) {
    QList<Pod> pods;
    QDir dir(repository);
    QString gitmodulesPath = dir.filePath(".gitmodules");
    if(QFile::exists(gitmodulesPath)) {
        QSettings gitmodules(gitmodulesPath, QSettings::IniFormat);
        gitmodules.setIniCodec("UTF-8");
        QStringList childGroups = gitmodules.childGroups();
        foreach(QString childGroup, childGroups) {
            if(childGroup.startsWith("submodule")) {
                gitmodules.beginGroup(childGroup);
                Pod pod;
                pod.name        = gitmodules.value("path").toString();
                pod.url         = gitmodules.value("url").toString();
                readPodInfo(repository, pod);
                pods.append(pod);
                gitmodules.endGroup();
            }
        }
    }
    emit installedPodsFinished(repository, pods);
    return pods;
}

QList<Pod> PodManager::availablePods(QStringList sources) {
    if(_networkAccessManager->networkAccessible() == QNetworkAccessManager::NotAccessible) {
        qDebug() << "No network connection available.";
        emit availablePodsFinished(sources, QList<Pod>());
        return QList<Pod>();
    }

    QList<Pod> pods;
    foreach(QString source, sources) {
        QNetworkRequest request;
        request.setUrl(QUrl(source));
        QNetworkReply *reply = _networkAccessManager->get(request);
        QEventLoop loop;
        connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
        loop.exec();

        QByteArray response = reply->readAll();

        if(reply->error() != QNetworkReply::NoError) {
            qDebug() << reply->errorString();
        }

        QJsonParseError parseError;
        QJsonDocument document = QJsonDocument::fromJson(response, &parseError);

        if(QJsonParseError::NoError == parseError.error) {
            QJsonObject object      = document.object();
            QStringList keys        = object.keys();

            foreach(QString key, keys) {
                if(object.value(key).isObject()) {
                    // New format
                    QJsonObject metaObject = object.value(key).toObject();
                    Pod pod;
                    pod.name        = key;
                    pod.url         = metaObject.value("url").toString();
                    pod.author      = metaObject.value("author").toString();
                    pod.description = metaObject.value("description").toString();
                    pod.license     = metaObject.value("license").toString();
                    pods.append(pod);

                } else {
                    // Old format: pod name and url
                    Pod pod;
                    pod.name        = key;
                    pod.url         = object.value(key).toString();
                    pods.append(pod);
                }
            }
        }
    }

    emit availablePodsFinished(sources, pods);
    return pods;
}

void PodManager::generatePodsPri(QString repository) {
    QList<Pod> pods = installedPods(repository);
    QString header = QString("# Auto-generated by qt-pods. Do not edit.\n# Include this to your application project file with:\n# include(../pods.pri)\n# This file should be put under version control.\n");
    QString includePris = "";
    foreach(Pod pod, pods) {
        includePris += QString("include(%1/%1.pri)\n").arg(pod.name);
    }

    QString podsPri = QString("%1\n%2\n")
        .arg(header)
        .arg(includePris);

    QFile file(QDir(repository).filePath("pods.pri"));
    file.remove();
    if(file.open(QFile::ReadWrite)) {
        file.write(podsPri.toUtf8());
        file.close();
    }
    stageFile(repository, file.fileName());

    emit generatePodsPriFinished(repository);
}

void PodManager::generatePodsSubdirsPri(QString repository) {
    QList<Pod> pods = installedPods(repository);
    QString header = QString("# Auto-generated by qt-pods. Do not edit.\n# Include this to your subdirs project file with:\n# include(pods-subdirs.pri)\n# This file should be put under version control.\n");
    QString subdirs = "SUBDIRS += ";

    foreach(Pod pod, pods) {
        subdirs += QString("\\\n\t%1 ").arg(pod.name);
    }

    QString podsSubdirsPri = QString("%1\n%2\n\n")
        .arg(header)
        .arg(subdirs);

    QFile file(QDir(repository).filePath("pods-subdirs.pri"));
    file.remove();
    if(file.open(QFile::ReadWrite)) {
        file.write(podsSubdirsPri.toUtf8());
        file.close();
    }
    stageFile(repository, file.fileName());

    emit generatePodsSubdirsPriFinished(repository);
}

void PodManager::generateSubdirsPro(QString repository) {
    QDir dir(repository);
    QFile file(QDir(repository).filePath(QString("%1.pro").arg(dir.dirName())));
    // Just create one if it doesn't exist yet.
    if(!file.exists()) {
        if(file.open(QFile::ReadWrite)) {
            QString subdirsPro = "# Auto-generated by qt-pods.\n# This file should be put under version control.\nTEMPLATE = subdirs\nSUBDIRS =\ninclude(pods-subdirs.pri)\n";
            file.write(subdirsPro.toUtf8());
            file.close();
        }
    }
    stageFile(repository, file.fileName());

    emit generateSubdirsProFinished(repository);
}

bool PodManager::checkPod(QString repository, QString podName) {
    QDir dir(repository);
    bool isValidPod = (podName == podName.toLower()) &&
            dir.cd(podName) &&
            QFile::exists(dir.filePath("LICENSE")) &&
            QFile::exists(dir.filePath("README.md")) &&
            QFile::exists(dir.filePath(podName + ".pri")) &&
            QFile::exists(dir.filePath(podName + ".pro"));
    emit checkPodFinished(repository, podName, isValidPod);
    return isValidPod;
}


void PodManager::purgePodInfo(QString repository, QString podName) {
    QDir dir(repository);
    QString podinfoPath = dir.filePath(".podinfo");
    QSettings podinfo(podinfoPath, QSettings::IniFormat);
    podinfo.setIniCodec("UTF-8");
    podinfo.remove(podName);
    podinfo.sync();

    stageFile(repository, ".podinfo");
}

void PodManager::writePodInfo(QString repository, Pod pod) {
    QDir dir(repository);
    QString podinfoPath = dir.filePath(".podinfo");
    QSettings podinfo(podinfoPath, QSettings::IniFormat);
    podinfo.setIniCodec("UTF-8");
    podinfo.beginGroup(pod.name);
        podinfo.setValue("author", pod.author);
        podinfo.setValue("description", pod.description);
        podinfo.setValue("license", pod.license);
        podinfo.setValue("website", pod.website);
        podinfo.endGroup();
    podinfo.sync();

    stageFile(repository, ".podinfo");
}

void PodManager::readPodInfo(QString repository, Pod& pod) {
    QDir dir(repository);
    QString podinfoPath = dir.filePath(".podinfo");
    QSettings podinfo(podinfoPath, QSettings::IniFormat);
    podinfo.setIniCodec("UTF-8");
    QStringList childGroups = podinfo.childGroups();
    if(childGroups.contains(pod.name)) {
        podinfo.beginGroup(pod.name);
        pod.author = podinfo.value("author").toString();
        pod.description = podinfo.value("description").toString();
        pod.license = podinfo.value("license").toString();
        pod.website = podinfo.value("website").toString();
        podinfo.endGroup();
    }
}

void PodManager::stageFile(QString repository, QString fileName) {
    QDir cwd = QDir::current();
    QDir::setCurrent(repository);

    QProcess::execute(QString("git add %1").arg(fileName));

    QDir::setCurrent(cwd.absolutePath());
}
