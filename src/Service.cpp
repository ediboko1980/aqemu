/****************************************************************************
**
** Copyright (C) 2016 Tobias Gläßer
**
** This file is part of AQEMU.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor,
** Boston, MA  02110-1301, USA.
**
****************************************************************************/

#include <QApplication>
#include <QResource>
#include <QMessageBox>
#include <QTranslator>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QtDBus>

#ifdef Q_OS_LINUX
#include <unistd.h>
#include <sys/types.h>
#endif
#include <iostream>

#include "Utils.h"

#include "Run_Guard.h"

#include "VM.h"
#include "Service.h"
#include "main.h"

AQEMU_Service::AQEMU_Service()
{
    service = nullptr;
    called_dbus = false;
    main_window = false;
}

AQEMU_Service::~AQEMU_Service()
{
    delete service;
}

void AQEMU_Service::setMainWindow( bool b )
{
    main_window = b;
}

void AQEMU_Service::vm_state_changed(Virtual_Machine *vm, VM::VM_State s)
{
    if ( s == VM::VMS_Power_Off )
    {
        machines.removeAll(vm);
        //delete vm; //FIXME? segfault
        if ( machines.count() < 1 )
        {
            if ( ! main_window )
            {
                QMetaObject::invokeMethod(QCoreApplication::instance(), "quit");
                //application->quit();
            }
        }
    }

    if (!QDBusConnection::sessionBus().isConnected()) {
        fprintf(stderr, "Cannot connect to the D-Bus session bus.\n"
                "To start it, run:\n"
                "\teval `dbus-launch --auto-syntax`\n");
        return;
    }

    QDBusInterface iface("org.aqemu.main_window", "/main_window", "", QDBusConnection::sessionBus());
    if (iface.isValid()) {
        /*QDBusReply<QString> reply =*/ iface.call(QDBus::NoBlock, "VM_State_Changed", vm->Get_VM_XML_File_Path(), s );
        /*if (reply.isValid()) {
            printf("Reply was: %s\n", qPrintable(reply.value()));*/
            return;
        /*}

        fprintf(stderr, "Call failed: %s\n", qPrintable(reply.error().message()));
        return;*/
    }

    fprintf(stderr, "%s\n",
            qPrintable(QDBusConnection::sessionBus().lastError().message()));

}

bool AQEMU_Service::isActive()
{
    return ( machines.count() > 0 );
}

bool AQEMU_Service::call(const QString& command, const QList<QVariant>& params, bool noblock)
{
    if ( params.count() < 1 )
        return false;

    called_dbus = true;
    init_service();

    if (!QDBusConnection::sessionBus().isConnected()) {
        fprintf(stderr, "Cannot connect to the D-Bus session bus.\n"
                "To start it, run:\n"
                "\teval `dbus-launch --auto-syntax`\n");
        return false;
    }

    QDBusInterface iface(SERVICE_NAME, "/", "", QDBusConnection::sessionBus());
    if (iface.isValid()) {
        if ( noblock )
        {
            iface.callWithArgumentList(QDBus::NoBlock, command, params);
        }
        else
        {
            QDBusReply<QString> reply = iface.callWithArgumentList(QDBus::AutoDetect, command, params);
            if (reply.isValid()) {
                printf("Reply was: %s\n", qPrintable(reply.value()));
                return true;
            }

            fprintf(stderr, "Call failed: %s\n", qPrintable(reply.error().message()));
            return false;
        }
    }

    fprintf(stderr, "%s\n",
            qPrintable(QDBusConnection::sessionBus().lastError().message()));
    return false;
}

bool AQEMU_Service::call(const QString &command, Virtual_Machine *vm, bool noblock)
{
    call(command, vm->Get_VM_XML_File_Path(), noblock);
}

bool AQEMU_Service::call(const QString &command, const QString& vm, bool noblock)
{
    QList<QVariant> list;
    list << vm;
    call(command, list, noblock);
}

bool AQEMU_Service::call(const QString &command, Virtual_Machine *vm, const QString& param2, bool noblock)
{
    QList<QVariant> list;
    list << vm->Get_VM_XML_File_Path();
    list << param2;
    call(command, list, noblock);
}


bool AQEMU_Service::init_service()
{
    std::cout << "init service" << std::endl;

    service = new Run_Guard( "Gmp[0Ab6000" ); //if service is already running, skip this
    if (service->tryToRun() == false)
    {
        service = nullptr;
        return false;
    }

    std::cout << "init service REALLY" << std::endl;

    //dbus listening stuff

    if (!QDBusConnection::sessionBus().isConnected()) {
        fprintf(stderr, "Cannot connect to the D-Bus session bus.\n"
                "To start it, run:\n"
                "\teval `dbus-launch --auto-syntax`\n");
        return false;
    }

    if (!QDBusConnection::sessionBus().registerService(SERVICE_NAME)) {
        fprintf(stderr, "%s\n",
                qPrintable(QDBusConnection::sessionBus().lastError().message()));
        return false;
    }

    std::cout << "registered" << std::endl;
    QDBusConnection::sessionBus().registerObject("/", this, QDBusConnection::ExportAllSlots);
    return true;
}

QString AQEMU_Service::start(const QString& s)
{
    auto vm = new Virtual_Machine;
    vm->Load_VM(s);

    if ( vm->Start() )
    {
        machines.append(vm);

        connect(vm,SIGNAL(State_Changed( Virtual_Machine*, VM::VM_State)),this,SLOT(vm_state_changed(Virtual_Machine*, VM::VM_State)));

        return QString("VM \"%1\" got started.").arg(s);
    }

    return QString("VM \"%1\" could not be started.");
}

QString AQEMU_Service::stop(const QString& s)
{
    for ( int i = 0; i < machines.count(); i++ )
    {
        if ( QFileInfo(machines.at(i)->Get_VM_XML_File_Path()) == QFileInfo(s) )
        {
            machines.at(i)->Stop();
            return QString("VM \"%1\" got stopped.").arg(s);
        }
    }

    return QString("VM \"%1\" could not be stopped.").arg(s);
}


QString AQEMU_Service::reset(const QString& s)
{
    for ( int i = 0; i < machines.count(); i++ )
    {
        if ( QFileInfo(machines.at(i)->Get_VM_XML_File_Path()) == QFileInfo(s) )
        {
            machines.at(i)->Reset();
            return QString("VM \"%1\" got reset.").arg(s);
        }
    }

    return QString("VM \"%1\" could  not be reset.").arg(s);
}


QString AQEMU_Service::pause(const QString& s)
{
    for ( int i = 0; i < machines.count(); i++ )
    {
        if ( QFileInfo(machines.at(i)->Get_VM_XML_File_Path()) == QFileInfo(s) )
        {
            machines.at(i)->Pause();
            return QString("VM \"%1\" got paused.").arg(s);
        }
    }

    return QString("VM \"%1\" could not be paused.").arg(s);
}

QString AQEMU_Service::save(const QString& s)
{
    for ( int i = 0; i < machines.count(); i++ )
    {
        if ( QFileInfo(machines.at(i)->Get_VM_XML_File_Path()) == QFileInfo(s) )
        {
            machines.at(i)->Save_VM_State();
            return QString("VM state of \"%1\" got saved.").arg(s);
        }
    }

    return QString("VM state of \"%1\" could not be saved.").arg(s);
}


QString AQEMU_Service::error(const QString& s)
{
    for ( int i = 0; i < machines.count(); i++ )
    {
        if ( QFileInfo(machines.at(i)->Get_VM_XML_File_Path()) == QFileInfo(s) )
        {
            machines.at(i)->Show_Error_Log_Window();
            return QString("VM error log window of \"%1\" got shown.").arg(s);
        }
    }

    return QString("VM error log window of \"%1\" could not be shown.").arg(s);
}

QString AQEMU_Service::control(const QString& s)
{
    for ( int i = 0; i < machines.count(); i++ )
    {
        if ( QFileInfo(machines.at(i)->Get_VM_XML_File_Path()) == QFileInfo(s) )
        {
            machines.at(i)->Show_Emu_Ctl_Win();
            return QString("VM control window of \"%1\" got shown.").arg(s);
        }
    }

    return QString("VM control window of \"%1\" could not be shown.").arg(s);
}

QString AQEMU_Service::status(const QString& s)
{
    for ( int i = 0; i < machines.count(); i++ )
    {
        if ( QFileInfo(machines.at(i)->Get_VM_XML_File_Path()) == QFileInfo(s) )
        {
            vm_state_changed(machines.at(i),machines.at(i)->Get_State());
            QString state = machines.at(i)->Get_State_Text();
            return QString("VM state:  %1.").arg(state);
        }
    }

    return QString("Could not show state of VM \"%1\".").arg(s);
}

QString AQEMU_Service::command(const QString &vm, const QString &command)
{
    for ( int i = 0; i < machines.count(); i++ )
    {
        if ( QFileInfo(machines.at(i)->Get_VM_XML_File_Path()) == QFileInfo(vm) )
        {
            machines.at(i)->Send_Emulator_Command(command);
            return QString("Sent command to VM \"%1\".").arg(vm);
        }
    }

    return QString("Could not send command to VM \"%1\".").arg(vm);
}
