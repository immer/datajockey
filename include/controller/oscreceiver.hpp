/*
 *		Copyright (c) 2012 Alex Norman.  All rights reserved.
 *		http://www.x37v.info/datajockey
 *
 *		This file is part of Data Jockey.
 *		
 *		Data Jockey is free software: you can redistribute it and/or modify it
 *		under the terms of the GNU General Public License as published by the
 *		Free Software Foundation, either version 3 of the License, or (at your
 *		option) any later version.
 *		
 *		Data Jockey is distributed in the hope that it will be useful, but
 *		WITHOUT ANY WARRANTY; without even the implied warranty of
 *		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 *		Public License for more details.
 *		
 *		You should have received a copy of the GNU General Public License along
 *		with Data Jockey.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DATAJOCKEY_OSC_RECEIVER_HPP
#define DATAJOCKEY_OSC_RECEIVER_HPP

#include "osc/OscPacketListener.h"
#include "timepoint.hpp"
#include <string>
#include <QThread>
#include <QObject>

using dj::audio::TimePoint;

class OSCReceiver : public QObject, public osc::OscPacketListener {
  Q_OBJECT
  public:
    OSCReceiver(QObject * parent = NULL);
    virtual ~OSCReceiver();
  protected:
    virtual void ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint);
  signals:
    void player_triggered(int player_index, QString name);
    void player_value_changed(int player_index, QString name, bool value);
    void player_value_changed(int player_index, QString name, int value);
    void player_value_changed(int player_index, QString name, double value);
    void player_value_changed(int player_index, QString name, QString value);

    void master_triggered(QString name);
    void master_value_changed(QString name, bool value);
    void master_value_changed(QString name, int value);
    void master_value_changed(QString name, double value);
    void master_value_changed(QString name, TimePoint timepoint);
};

class OscThread : public QThread {
  Q_OBJECT
  public:
    OscThread(unsigned int port);
    void run();
    OSCReceiver *receiver() { return &mOSCReceiver; }
  private:
    OSCReceiver mOSCReceiver;
    unsigned int mPort;
};

#endif