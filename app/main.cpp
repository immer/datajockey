#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QThread>
#include <QTimer>
#include <QErrorMessage>
#include <QFileInfo>
#include <iostream>

#include "db.h"
#include "audiomodel.h"
#include "audioloader.h"
#include "defines.hpp"
#include "midirouter.h"
#include "config.hpp"
#include "oscsender.h"
#include "historymanager.h"
#include "nsm.h"

#include <signal.h>

//nsm code taken [and edited] from Harry van Haaren's article:
// http://www.openavproductions.com/articles/nsm/

static int nsm_open_cb(const char *name, const char *display_name, const char *client_id, char **out_msg, void *userdata);
static int nsm_save_cb(char **out_msg, void *userdata);
static void signalHanlder(int sigNum);
static int startApp(QApplication * app, QString jackClientName, nsm_client_t * nsm_client);

namespace {
  bool quit_app = false;
  bool start_app = false;
}
QString clientName("datajockey");

int main(int argc, char *argv[])
{
  QApplication a(argc, argv);
  QApplication::setStyle(QStyleFactory::create("Fusion"));

  qRegisterMetaType<dj::loop_and_jump_type_t>("dj::loop_and_jump_type_t");

  a.setApplicationVersion(dj::version_string);
  a.setApplicationName("Data Jockey " + a.applicationVersion());

  //for global qsettings
  a.setOrganizationName("xnor");
  a.setOrganizationDomain("x37v.info");
  a.setApplicationName("DataJockey");

  QPalette palette;
  palette.setColor(QPalette::Window, QColor(53,53,53));
  palette.setColor(QPalette::WindowText, Qt::white);
  palette.setColor(QPalette::Base, QColor(15,15,15));
  palette.setColor(QPalette::AlternateBase, QColor(53,53,53));
  palette.setColor(QPalette::ToolTipBase, Qt::white);
  palette.setColor(QPalette::ToolTipText, Qt::white);
  palette.setColor(QPalette::Text, Qt::white);
  palette.setColor(QPalette::Button, QColor(53,53,53));
  palette.setColor(QPalette::ButtonText, Qt::white);
  palette.setColor(QPalette::BrightText, Qt::red);

  palette.setColor(QPalette::Highlight, QColor(142,45,197).lighter());
  palette.setColor(QPalette::HighlightedText, Qt::black);
  a.setPalette(palette);

  QFile file(":/resources/style.qss");
  if(file.open(QFile::ReadOnly)){
    QString styleSheet = QLatin1String(file.readAll());
    a.setStyleSheet(styleSheet);
  }

  //setup NSM, if we can, wait for nsm to create the client
  nsm_client_t* nsm = 0;
  //try to attach to nsm
  const char *nsm_url = getenv( "NSM_URL" );
  if (nsm_url) {
    nsm = nsm_new();
    nsm_set_open_callback(nsm, nsm_open_cb, &a);
    nsm_set_save_callback(nsm, nsm_save_cb, nullptr);
    if (nsm_init(nsm, nsm_url) == 0) {
      //non session manager uses this to quit
      signal(SIGTERM, signalHanlder);
      nsm_send_announce(nsm, "datajockey", "", argv[0]);

      QTimer * quit_watch_timer = new QTimer();

      auto quit_func = [quit_watch_timer] (void) {
        if (!quit_app)
          return;
        quit_app = false;
        quit_watch_timer->stop();
        qApp->quit();
      };

      QObject::connect(quit_watch_timer, &QTimer::timeout, quit_func);
      quit_watch_timer->setInterval(500);
      quit_watch_timer->start(500);
    } else {
      nsm_free(nsm);
      nsm = 0;
    }
  }
  if (!nsm)
    return startApp(&a, clientName, nullptr);

  while (1) {
    sleep(1);
    nsm_check_nowait(nsm);
    if (start_app)
      return startApp(&a, clientName, nsm);
  }
  return 0;
}

static int startApp(QApplication * app, QString jackClientName, nsm_client_t * nsm_client) {
  dj::Configuration * config = dj::Configuration::instance();
  config->load_default();

  DB * db = new DB(config->db_adapter(), config->db_name(), config->db_username(), config->db_password(), config->db_port(), config->db_host());
  AudioModel * audio = new AudioModel();
  audio->setDB(db);
  audio->createClient(jackClientName);
  audio->run(true);

  AudioLoader * loader = new AudioLoader(db, audio);
  QObject::connect(loader, &AudioLoader::playerBuffersChanged, audio, &AudioModel::playerLoad);
  QObject::connect(loader, &AudioLoader::playerValueChangedInt, audio, &AudioModel::playerSetValueInt);
  QObject::connect(loader, &AudioLoader::playerValueChangedString,
      [audio](int player, QString name, QString /*value*/) {
        if (name == "loading_work")
          audio->playerClear(player);
      });

  //set up the session history logging
  HistoryManager * history = new HistoryManager(audio->playerCount(), audio);
  QObject::connect(loader, &AudioLoader::playerValueChangedInt, history, &HistoryManager::playerSetValueInt);
  QObject::connect(audio, &AudioModel::playerValueChangedBool, history, &HistoryManager::playerSetValueBool);
  //XXX can we move this insert into the background in order to ditch the pause we get in the GUI?
  QObject::connect(history, &HistoryManager::workHistoryChanged, db, &DB::work_set_played);

  MidiRouter * midi = new MidiRouter(audio->audioio()->midi_input_ringbuffer());
  QThread * midiThread = new QThread;
  midi->moveToThread(midiThread);
  QTimer * midiProcessTimer = new QTimer();
  midiProcessTimer->setInterval(15);
  QObject::connect(midiProcessTimer, &QTimer::timeout, midi, &MidiRouter::process);
  QObject::connect(midiThread, &QThread::finished, midiProcessTimer, &QTimer::stop);
  midiThread->start(QThread::HighPriority);
  midiProcessTimer->start();

  QObject::connect(midi, &MidiRouter::playerValueChangedDouble, audio, &AudioModel::playerSetValueDouble);
  QObject::connect(midi, &MidiRouter::playerValueChangedInt,    audio, &AudioModel::playerSetValueInt);
  QObject::connect(midi, &MidiRouter::playerValueChangedBool,   audio, &AudioModel::playerSetValueBool);
  QObject::connect(midi, &MidiRouter::playerTriggered,          audio, &AudioModel::playerTrigger);

  QObject::connect(midi, &MidiRouter::masterValueChangedDouble, audio, &AudioModel::masterSetValueDouble);
  QObject::connect(midi, &MidiRouter::masterValueChangedInt,    audio, &AudioModel::masterSetValueInt);
  QObject::connect(midi, &MidiRouter::masterValueChangedBool,   audio, &AudioModel::masterSetValueBool);
  QObject::connect(midi, &MidiRouter::masterTriggered,          audio, &AudioModel::masterTrigger);
  QObject::connect(midi, &MidiRouter::playerTriggered, loader, &AudioLoader::playerTrigger);

  QErrorMessage * midiErrors = new QErrorMessage;
  QObject::connect(midi, &MidiRouter::mappingError, midiErrors, static_cast<void (QErrorMessage::*)(const QString&)>(&QErrorMessage::showMessage));

  if (QFileInfo::exists(config->midi_mapping_file()))
    midi->readFile(config->midi_mapping_file());

  OSCSender * osc_send = new OSCSender();
  QThread * osc_send_thread = new QThread;
  osc_send_thread->moveToThread(osc_send_thread);
  osc_send_thread->start();

  //XXX tmp
  osc_send->setHostAndPort("", "10101");

  QObject::connect(audio, &AudioModel::playerValueChangedDouble, osc_send, &OSCSender::playerSetValueDouble);
  QObject::connect(audio, &AudioModel::playerValueChangedInt,    osc_send, &OSCSender::playerSetValueInt);
  QObject::connect(audio, &AudioModel::playerValueChangedBool,   osc_send, &OSCSender::playerSetValueBool);
  QObject::connect(audio, &AudioModel::playerTriggered,          osc_send, &OSCSender::playerTrigger);

  QObject::connect(audio, &AudioModel::masterValueChangedDouble, osc_send, &OSCSender::masterSetValueDouble);
  QObject::connect(audio, &AudioModel::masterValueChangedInt,    osc_send, &OSCSender::masterSetValueInt);
  QObject::connect(audio, &AudioModel::masterValueChangedBool,   osc_send, &OSCSender::masterSetValueBool);
  QObject::connect(audio, &AudioModel::masterTriggered,          osc_send, &OSCSender::masterTrigger);


  MainWindow * w = new MainWindow(db, audio);
  w->loader(loader);
  QObject::connect(history, &HistoryManager::workHistoryChanged, w, &MainWindow::workUpdateHistory);
  QObject::connect(midi, &MidiRouter::masterValueChangedInt,     w, &MainWindow::masterSetValueInt);

  //set master volume to 0.8
  QTimer * del = new QTimer(app);
  del->setSingleShot(true);
  QObject::connect(del, &QTimer::timeout, [audio]() {
    audio->masterSetValueInt("volume", dj::to_int(0.8));
  });
  del->start(10);

  QObject::connect(app, &QApplication::aboutToQuit, [audio, w, midiThread] {
    w->finalize();
    midiThread->quit();
    audio->prepareToQuit();
    QThread::msleep(200);
  });
  w->show();

  if (nsm_client) {
    QTimer * nsmTimer = new QTimer();
    QObject::connect(nsmTimer, &QTimer::timeout, [nsm_client]() {
        nsm_check_nowait(nsm_client);
    });
    QObject::connect(app, &QApplication::aboutToQuit, nsmTimer, &QTimer::stop);

    nsmTimer->setSingleShot(false);
    nsmTimer->setInterval(1000);
    nsmTimer->start(1000);
  }

  return app->exec();
}

static int nsm_open_cb(const char * /*name */,
    const char * /*display_name*/,
    const char *client_id,
    char ** /*out_msg */,
    void * /*userdata */)
{
  clientName = QString(client_id);
  start_app = true;
  return ERR_OK;
}

static int nsm_save_cb(char ** /*out_msg*/, void * /*userdata*/) {
  return 0; //doesn't do anything
}

void signalHanlder(int sigNum) {
  if (sigNum == SIGTERM)
    quit_app = true;
}

